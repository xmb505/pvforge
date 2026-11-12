#include "next/settings/GitDetect.h"
#include "next/settings/OnboardingDialog.h"
#include "next/settings/SettingsDialog.h"
#include "next/settings/SettingsStore.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QtTest>

using namespace pvf::next;

namespace {

constexpr qint64 kMiB = 1024LL * 1024;
constexpr qint64 kGiB = 1024LL * 1024 * 1024;

template <typename T>
T* findWidget(QWidget& parent, const char* name)
{
    T* widget = parent.findChild<T*>(QString::fromLatin1(name));
    if (!widget)
        qFatal("missing widget %s", name);
    return widget;
}

void verifySettingsEqual(const AppSettings& actual, const AppSettings& expected)
{
    QCOMPARE(actual.htmlCacheEnabled, expected.htmlCacheEnabled);
    QCOMPARE(actual.htmlMemoryBytes, expected.htmlMemoryBytes);
    QCOMPARE(actual.htmlDiskBytes, expected.htmlDiskBytes);
    QCOMPARE(actual.htmlScale, expected.htmlScale);
    QCOMPARE(actual.htmlMaxWidth, expected.htmlMaxWidth);
    QCOMPARE(actual.watchdogMs, expected.watchdogMs);
    QCOMPARE(actual.chromeExecutable, expected.chromeExecutable);
    QCOMPARE(actual.videoProxyEnabled, expected.videoProxyEnabled);
    QCOMPARE(actual.videoProxyWidth, expected.videoProxyWidth);
    QCOMPARE(actual.videoMemoryFrames, expected.videoMemoryFrames);
    QCOMPARE(actual.workbenchCacheEnabled, expected.workbenchCacheEnabled);
    QCOMPARE(actual.workbenchCacheScalePercent, expected.workbenchCacheScalePercent);
    QCOMPARE(actual.workbenchCacheExpandPercent, expected.workbenchCacheExpandPercent);
    QCOMPARE(actual.workbenchCacheDiskBytes, expected.workbenchCacheDiskBytes);
    QCOMPARE(actual.workbenchCacheMemoryBytes, expected.workbenchCacheMemoryBytes);
    QCOMPARE(actual.workbenchCacheWriterThreads,
             expected.workbenchCacheWriterThreads);
    QCOMPARE(actual.snapPixels, expected.snapPixels);
    QCOMPARE(actual.snapCanvasCenter, expected.snapCanvasCenter);
    QCOMPARE(actual.snapEntityEdges, expected.snapEntityEdges);
    QCOMPARE(actual.snapEdgeMidpoints, expected.snapEdgeMidpoints);
    QVERIFY(actual.snapAngles == expected.snapAngles);
    QCOMPARE(int(actual.mcpPort), int(expected.mcpPort));
    QCOMPARE(actual.humanCommitDelayMs, expected.humanCommitDelayMs);
    QCOMPARE(actual.autoSaveMinutes, expected.autoSaveMinutes);
    QCOMPARE(actual.gitEnabled, expected.gitEnabled);
    QCOMPARE(actual.onboardingDone, expected.onboardingDone);
}

} // namespace

class SettingsTests : public QObject
{
    Q_OBJECT

private slots:
    void storeDefaults();
    void storeGitDefaults();
    void storeRoundTrip();
    void storeClampsLoadedValues();
    void storeSanitizesSavedValues();
    void workbenchDefaults();
    void workbenchStoreRoundTrip();
    void workbenchStoreSanitizes();
    void workbenchMemoryMatchesUiRange();
    void editorSnapDefaults();
    void editorSnapRoundTrip();

    void detectGitFindsExecutable();

    void dialogDefaults();
    void dialogRoundTrip();
    void dialogClampsValues();
    void editorDialogRoundTrip();
    void workbenchDialogRoundTrip();
    void workbenchDialogCancelDoesNotSave();
    void workbenchDialogApplySaves();
    void workbenchDialogSnapsAndClamps();
    void dialogGitCheckboxRoundTrip();
    void dialogGitCancelDoesNotSave();
    void dialogGitApplySaves();
    void cancelDoesNotSave();
    void applySavesWithoutClosing();
    void okSavesAndAccepts();
    void operationSignalsAreEmitted();
    void gitInfoAndEndpointPath();

    void onboardingDefaultsToUnchecked();
    void onboardingEnablesGit();
    void onboardingUsesConfiguredValue();
};

void SettingsTests::storeDefaults()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    SettingsStore store(temp.filePath(QStringLiteral("settings.ini")));

    verifySettingsEqual(store.load(), AppSettings{});
}

void SettingsTests::storeGitDefaults()
{
    const AppSettings defaults;
    QCOMPARE(defaults.gitEnabled, false);
    QCOMPARE(defaults.onboardingDone, false);
}

void SettingsTests::storeRoundTrip()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = temp.filePath(QStringLiteral("nested/settings.ini"));
    SettingsStore store(path);

    AppSettings expected;
    expected.htmlCacheEnabled = false;
    expected.htmlMemoryBytes = 512 * kMiB;
    expected.htmlDiskBytes = 8 * kGiB;
    expected.htmlScale = 0.75;
    expected.htmlMaxWidth = 1920;
    expected.watchdogMs = 4500;
    expected.chromeExecutable = QStringLiteral("/opt/google/chrome/google-chrome");
    expected.videoProxyEnabled = false;
    expected.videoProxyWidth = 1280;
    expected.videoMemoryFrames = 128;
    expected.mcpPort = 51234;
    expected.humanCommitDelayMs = 2500;
    expected.gitEnabled = true;
    expected.onboardingDone = true;

    store.save(expected);

    SettingsStore reopened(path);
    verifySettingsEqual(reopened.load(), expected);

    QSettings raw(path, QSettings::IniFormat);
    QVERIFY(raw.contains(QStringLiteral("html/cacheEnabled")));
    QVERIFY(raw.contains(QStringLiteral("html/memoryBytes")));
    QVERIFY(raw.contains(QStringLiteral("html/diskBytes")));
    QVERIFY(raw.contains(QStringLiteral("html/scale")));
    QVERIFY(raw.contains(QStringLiteral("html/maxWidth")));
    QVERIFY(raw.contains(QStringLiteral("html/watchdogMs")));
    QVERIFY(raw.contains(QStringLiteral("html/chromeExecutable")));
    QVERIFY(raw.contains(QStringLiteral("video/proxyEnabled")));
    QVERIFY(raw.contains(QStringLiteral("video/proxyWidth")));
    QVERIFY(raw.contains(QStringLiteral("video/memoryFrames")));
    QVERIFY(raw.contains(QStringLiteral("mcp/port")));
    QVERIFY(raw.contains(QStringLiteral("git/humanCommitDelayMs")));
    QVERIFY(raw.contains(QStringLiteral("git/enabled")));
    QVERIFY(raw.contains(QStringLiteral("app/onboardingDone")));
    QCOMPARE(raw.value(QStringLiteral("html/memoryBytes")).toLongLong(), 512 * kMiB);
    QCOMPARE(raw.value(QStringLiteral("mcp/port")).toInt(), 51234);
    QCOMPARE(raw.value(QStringLiteral("git/enabled")).toBool(), true);
    QCOMPARE(raw.value(QStringLiteral("app/onboardingDone")).toBool(), true);
}

void SettingsTests::editorSnapDefaults()
{
    const AppSettings defaults;
    QCOMPARE(defaults.snapPixels, 6);
    QCOMPARE(defaults.snapCanvasCenter, true);
    QCOMPARE(defaults.snapEntityEdges, true);
    QCOMPARE(defaults.snapEdgeMidpoints, false);
    QVERIFY(defaults.snapAngles == QVector<double>({0.0, 90.0, 180.0, 270.0}));

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    SettingsStore store(temp.filePath(QStringLiteral("settings.ini")));
    verifySettingsEqual(store.load(), defaults);
}

void SettingsTests::editorSnapRoundTrip()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = temp.filePath(QStringLiteral("settings.ini"));
    SettingsStore store(path);

    AppSettings expected;
    expected.snapPixels = 18;
    expected.snapCanvasCenter = false;
    expected.snapEntityEdges = false;
    expected.snapEdgeMidpoints = true;
    expected.snapAngles = {0.0, 30.0, 45.0, 90.0, 180.0, 270.0};
    store.save(expected);

    SettingsStore reopened(path);
    verifySettingsEqual(reopened.load(), expected);

    QSettings raw(path, QSettings::IniFormat);
    QCOMPARE(raw.value(QStringLiteral("snap/pixels")).toInt(), 18);
    QCOMPARE(raw.value(QStringLiteral("snap/canvasCenter")).toBool(), false);
    QCOMPARE(raw.value(QStringLiteral("snap/entityEdges")).toBool(), false);
    QCOMPARE(raw.value(QStringLiteral("snap/edgeMidpoints")).toBool(), true);
    QCOMPARE(raw.value(QStringLiteral("snap/angles")).toString(),
             QStringLiteral("0,30,45,90,180,270"));

    AppSettings clamped;
    clamped.snapPixels = 99;
    const AppSettings sanitized = SettingsStore::sanitized(clamped);
    QCOMPARE(sanitized.snapPixels, 32);
    clamped.snapPixels = 0;
    QCOMPARE(SettingsStore::sanitized(clamped).snapPixels, 1);

    clamped.snapAngles = {90.0, -30.0, 90.0, 450.0};
    QVERIFY(SettingsStore::sanitized(clamped).snapAngles
            == QVector<double>({90.0, 330.0}));
}

void SettingsTests::detectGitFindsExecutable()
{
    const GitInfo info = detectGit();
    if (!info.found)
        QSKIP(qPrintable(QStringLiteral("本机没有 git：%1").arg(info.error)));

    QVERIFY(!info.executable.isEmpty());
    QVERIFY2(QFileInfo(info.executable).isAbsolute(), qPrintable(info.executable));
    QVERIFY2(info.version.contains(QStringLiteral("git version")),
             qPrintable(info.version));
    QVERIFY(info.error.isEmpty());
}

void SettingsTests::storeClampsLoadedValues()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = temp.filePath(QStringLiteral("settings.ini"));
    {
        QSettings raw(path, QSettings::IniFormat);
        raw.setValue(QStringLiteral("html/cacheEnabled"), false);
        raw.setValue(QStringLiteral("html/memoryBytes"), Q_INT64_C(1));
        raw.setValue(QStringLiteral("html/diskBytes"), Q_INT64_C(0));
        raw.setValue(QStringLiteral("html/scale"), 0.6);
        raw.setValue(QStringLiteral("html/maxWidth"), 99999);
        raw.setValue(QStringLiteral("html/watchdogMs"), 10);
        raw.setValue(QStringLiteral("video/proxyEnabled"), false);
        raw.setValue(QStringLiteral("video/proxyWidth"), 1000);
        raw.setValue(QStringLiteral("video/memoryFrames"), 1);
        raw.setValue(QStringLiteral("mcp/port"), 80);
        raw.setValue(QStringLiteral("git/humanCommitDelayMs"), 99999);
        raw.setValue(QStringLiteral("app/autoSaveMinutes"), 0);
        raw.sync();
    }

    SettingsStore store(path);
    const AppSettings settings = store.load();

    QCOMPARE(settings.htmlCacheEnabled, false);
    QCOMPARE(settings.htmlMemoryBytes, 32 * kMiB);
    QCOMPARE(settings.htmlDiskBytes, 1 * kGiB);
    QCOMPARE(settings.htmlScale, 0.5);
    QCOMPARE(settings.htmlMaxWidth, 4096);
    QCOMPARE(settings.watchdogMs, 500);
    QCOMPARE(settings.videoProxyEnabled, false);
    QCOMPARE(settings.videoProxyWidth, 960);
    QCOMPARE(settings.videoMemoryFrames, 32);
    QCOMPARE(int(settings.mcpPort), 1024);
    QCOMPARE(settings.humanCommitDelayMs, 5000);
    QCOMPARE(settings.autoSaveMinutes, 1);
}

void SettingsTests::storeSanitizesSavedValues()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    SettingsStore store(temp.filePath(QStringLiteral("settings.ini")));

    AppSettings invalid;
    invalid.htmlMemoryBytes = -1;
    invalid.htmlDiskBytes = 1024 * kGiB;
    invalid.htmlScale = 0.9;
    invalid.htmlMaxWidth = 1;
    invalid.watchdogMs = 1;
    invalid.videoProxyWidth = 4096;
    invalid.videoMemoryFrames = 999;
    invalid.mcpPort = 65535;
    invalid.humanCommitDelayMs = 0;
    invalid.autoSaveMinutes = 999;
    store.save(invalid);

    const AppSettings settings = store.load();
    QCOMPARE(settings.htmlMemoryBytes, 32 * kMiB);
    QCOMPARE(settings.htmlDiskBytes, 64 * kGiB);
    QCOMPARE(settings.htmlScale, 1.0);
    QCOMPARE(settings.htmlMaxWidth, 320);
    QCOMPARE(settings.watchdogMs, 500);
    QCOMPARE(settings.videoProxyWidth, 1280);
    QCOMPARE(settings.videoMemoryFrames, 128);
    QCOMPARE(int(settings.mcpPort), 65535);
    QCOMPARE(settings.humanCommitDelayMs, 200);
}

void SettingsTests::workbenchDefaults()
{
    const AppSettings defaults;
    QCOMPARE(defaults.workbenchCacheEnabled, true);
    QCOMPARE(defaults.workbenchCacheScalePercent, 50);
    QCOMPARE(defaults.workbenchCacheExpandPercent, 50);
    QCOMPARE(defaults.workbenchCacheDiskBytes, 8 * kGiB);
    QCOMPARE(defaults.workbenchCacheMemoryBytes, 256 * kMiB);
    QCOMPARE(defaults.workbenchCacheWriterThreads, 2);

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    SettingsStore store(temp.filePath(QStringLiteral("settings.ini")));
    verifySettingsEqual(store.load(), defaults);
}

void SettingsTests::workbenchStoreRoundTrip()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = temp.filePath(QStringLiteral("settings.ini"));
    SettingsStore store(path);

    AppSettings expected;
    expected.workbenchCacheEnabled = false;
    expected.workbenchCacheScalePercent = 75;
    expected.workbenchCacheExpandPercent = 125;
    expected.workbenchCacheDiskBytes = 24 * kGiB;
    expected.workbenchCacheMemoryBytes = 512 * kMiB;
    expected.workbenchCacheWriterThreads = 3;
    store.save(expected);

    SettingsStore reopened(path);
    const AppSettings loaded = reopened.load();
    QCOMPARE(loaded.workbenchCacheEnabled, false);
    QCOMPARE(loaded.workbenchCacheScalePercent, 75);
    QCOMPARE(loaded.workbenchCacheExpandPercent, 125);
    QCOMPARE(loaded.workbenchCacheDiskBytes, 24 * kGiB);
    QCOMPARE(loaded.workbenchCacheMemoryBytes, 512 * kMiB);
    QCOMPARE(loaded.workbenchCacheWriterThreads, 3);

    QSettings raw(path, QSettings::IniFormat);
    QCOMPARE(raw.value(QStringLiteral("workbench/enabled")).toBool(), false);
    QCOMPARE(raw.value(QStringLiteral("workbench/scalePercent")).toInt(), 75);
    QCOMPARE(raw.value(QStringLiteral("workbench/expandPercent")).toInt(), 125);
    QCOMPARE(raw.value(QStringLiteral("workbench/diskBytes")).toLongLong(), 24 * kGiB);
    QCOMPARE(raw.value(QStringLiteral("workbench/memoryBytes")).toLongLong(), 512 * kMiB);
    QCOMPARE(raw.value(QStringLiteral("workbench/writerThreads")).toInt(), 3);
}

void SettingsTests::workbenchStoreSanitizes()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = temp.filePath(QStringLiteral("settings.ini"));
    {
        QSettings raw(path, QSettings::IniFormat);
        raw.setValue(QStringLiteral("workbench/scalePercent"), 60);
        raw.setValue(QStringLiteral("workbench/expandPercent"), 900);
        raw.setValue(QStringLiteral("workbench/diskBytes"), Q_INT64_C(0));
        raw.setValue(QStringLiteral("workbench/memoryBytes"), Q_INT64_C(1));
        raw.setValue(QStringLiteral("workbench/writerThreads"), 0);
        raw.sync();
    }

    SettingsStore store(path);
    const AppSettings loaded = store.load();
    QCOMPARE(loaded.workbenchCacheScalePercent, 50);
    QCOMPARE(loaded.workbenchCacheExpandPercent, 200);
    QCOMPARE(loaded.workbenchCacheDiskBytes, 1 * kGiB);
    QCOMPARE(loaded.workbenchCacheMemoryBytes, 32 * kMiB);
    QCOMPARE(loaded.workbenchCacheWriterThreads, 1);

    const auto snapped = [](int percent) {
        AppSettings settings;
        settings.workbenchCacheScalePercent = percent;
        return SettingsStore::sanitized(settings).workbenchCacheScalePercent;
    };
    QCOMPARE(snapped(10), 25);
    QCOMPARE(snapped(60), 50);
    QCOMPARE(snapped(70), 75);
    QCOMPARE(snapped(90), 100);
    QCOMPARE(snapped(1000), 100);

    const auto expanded = [](int percent) {
        AppSettings settings;
        settings.workbenchCacheExpandPercent = percent;
        return SettingsStore::sanitized(settings).workbenchCacheExpandPercent;
    };
    QCOMPARE(expanded(-10), 0);
    QCOMPARE(expanded(0), 0);
    QCOMPARE(expanded(50), 50);
    QCOMPARE(expanded(200), 200);
    QCOMPARE(expanded(201), 200);

    AppSettings tooBig;
    tooBig.workbenchCacheDiskBytes = 1024 * kGiB;
    tooBig.workbenchCacheMemoryBytes = 1024 * kGiB;
    const AppSettings clamped = SettingsStore::sanitized(tooBig);
    QCOMPARE(clamped.workbenchCacheDiskBytes, 64 * kGiB);
    QCOMPARE(clamped.workbenchCacheMemoryBytes, 4096 * kMiB);

    // Writer threads are a hard 1..3 choice, not a snap: hand-edited INI
    // values outside the range clamp to the nearest supported end.
    const auto threads = [](int value) {
        AppSettings settings;
        settings.workbenchCacheWriterThreads = value;
        return SettingsStore::sanitized(settings).workbenchCacheWriterThreads;
    };
    QCOMPARE(threads(-5), 1);
    QCOMPARE(threads(0), 1);
    QCOMPARE(threads(1), 1);
    QCOMPARE(threads(2), 2);
    QCOMPARE(threads(3), 3);
    QCOMPARE(threads(9), 3);
}

void SettingsTests::workbenchMemoryMatchesUiRange()
{
    // The cache page exposes the memory budget as a 32..4096 MB spin box; the
    // store keeps the same byte range and carries arbitrary MB values through.
    QCOMPARE(AppSettings{}.workbenchCacheMemoryBytes, 256 * kMiB);

    const auto bytes = [](qint64 value) {
        AppSettings settings;
        settings.workbenchCacheMemoryBytes = value;
        return SettingsStore::sanitized(settings).workbenchCacheMemoryBytes;
    };
    QCOMPARE(bytes(-1), 32 * kMiB);
    QCOMPARE(bytes(32 * kMiB), 32 * kMiB);
    QCOMPARE(bytes(513 * kMiB), 513 * kMiB);
    QCOMPARE(bytes(4096 * kMiB), 4096 * kMiB);
    QCOMPARE(bytes(4097 * kMiB), 4096 * kMiB);
}

void SettingsTests::dialogDefaults()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    SettingsStore store(temp.filePath(QStringLiteral("settings.ini")));
    SettingsDialog dialog(&store);

    verifySettingsEqual(dialog.settings(), AppSettings{});
    QCOMPARE(findWidget<QSpinBox>(dialog, "autoSaveMinutes")->value(), 5);
    QCOMPARE(findWidget<QSpinBox>(dialog, "autoSaveMinutes")->minimum(), 1);
    QCOMPARE(findWidget<QSpinBox>(dialog, "autoSaveMinutes")->maximum(), 60);
    QCOMPARE(findWidget<QTabWidget>(dialog, "settingsTabs")->count(), 7);
    QVERIFY(!findWidget<QCheckBox>(dialog, "gitEnabled")->isChecked());
    QVERIFY(!findWidget<QLabel>(dialog, "gitStatus")->text().isEmpty());
    QVERIFY(dialog.endpointPath().endsWith(
        QStringLiteral("/PVForge/mcp-endpoint.json")));
    QCOMPARE(dialog.endpointPath(), SettingsDialog::defaultEndpointPath());
    QCOMPARE(dialog.gitRepositoryPath(), QString());
    QCOMPARE(dialog.gitRecentCommit(), QString());
}

void SettingsTests::dialogRoundTrip()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    SettingsStore store(temp.filePath(QStringLiteral("settings.ini")));
    SettingsDialog dialog(&store);

    AppSettings expected;
    expected.htmlCacheEnabled = false;
    expected.htmlMemoryBytes = 1024 * kMiB;
    expected.htmlDiskBytes = 16 * kGiB;
    expected.htmlScale = 1.0;
    expected.htmlMaxWidth = 2560;
    expected.watchdogMs = 8000;
    expected.chromeExecutable = QStringLiteral("/usr/bin/chromium");
    expected.videoProxyEnabled = false;
    expected.videoProxyWidth = 1280;
    expected.videoMemoryFrames = 32;
    expected.mcpPort = 50000;
    expected.humanCommitDelayMs = 3000;

    dialog.setSettings(expected);
    verifySettingsEqual(dialog.settings(), expected);
}

void SettingsTests::dialogClampsValues()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    SettingsStore store(temp.filePath(QStringLiteral("settings.ini")));
    SettingsDialog dialog(&store);

    AppSettings invalid;
    invalid.htmlMemoryBytes = 1;
    invalid.htmlDiskBytes = 0;
    invalid.htmlScale = 0.6;
    invalid.htmlMaxWidth = 1;
    invalid.watchdogMs = 1;
    invalid.videoProxyWidth = 1000;
    invalid.videoMemoryFrames = 1;
    invalid.mcpPort = 80;
    invalid.humanCommitDelayMs = 1;
    invalid.autoSaveMinutes = 999;

    dialog.setSettings(invalid);
    const AppSettings settings = dialog.settings();
    QCOMPARE(settings.htmlMemoryBytes, 32 * kMiB);
    QCOMPARE(settings.htmlDiskBytes, 1 * kGiB);
    QCOMPARE(settings.htmlScale, 0.5);
    QCOMPARE(settings.htmlMaxWidth, 320);
    QCOMPARE(settings.watchdogMs, 500);
    QCOMPARE(settings.videoProxyWidth, 960);
    QCOMPARE(settings.videoMemoryFrames, 32);
    QCOMPARE(int(settings.mcpPort), 1024);
    QCOMPARE(settings.humanCommitDelayMs, 200);
    QCOMPARE(settings.autoSaveMinutes, 60);
}

void SettingsTests::editorDialogRoundTrip()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    SettingsStore store(temp.filePath(QStringLiteral("settings.ini")));
    SettingsDialog dialog(&store);

    QCOMPARE(findWidget<QSpinBox>(dialog, "snapPixels")->value(), 6);
    QCOMPARE(findWidget<QSpinBox>(dialog, "snapPixels")->minimum(), 1);
    QCOMPARE(findWidget<QSpinBox>(dialog, "snapPixels")->maximum(), 32);
    QVERIFY(findWidget<QCheckBox>(dialog, "snapCanvasCenter")->isChecked());
    QVERIFY(findWidget<QCheckBox>(dialog, "snapEntityEdges")->isChecked());
    QVERIFY(!findWidget<QCheckBox>(dialog, "snapEdgeMidpoints")->isChecked());
    QVERIFY(findWidget<QCheckBox>(dialog, "snapAngle0")->isChecked());
    QVERIFY(!findWidget<QCheckBox>(dialog, "snapAngle30")->isChecked());
    QVERIFY(!findWidget<QCheckBox>(dialog, "snapAngle45")->isChecked());
    QVERIFY(findWidget<QCheckBox>(dialog, "snapAngle90")->isChecked());

    findWidget<QSpinBox>(dialog, "snapPixels")->setValue(20);
    findWidget<QSpinBox>(dialog, "autoSaveMinutes")->setValue(12);
    findWidget<QCheckBox>(dialog, "snapCanvasCenter")->setChecked(false);
    findWidget<QCheckBox>(dialog, "snapEntityEdges")->setChecked(false);
    findWidget<QCheckBox>(dialog, "snapEdgeMidpoints")->setChecked(true);
    findWidget<QCheckBox>(dialog, "snapAngle30")->setChecked(true);
    findWidget<QCheckBox>(dialog, "snapAngle45")->setChecked(true);

    const AppSettings edited = dialog.settings();
    QCOMPARE(edited.snapPixels, 20);
    QCOMPARE(edited.autoSaveMinutes, 12);
    QCOMPARE(edited.snapCanvasCenter, false);
    QCOMPARE(edited.snapEntityEdges, false);
    QCOMPARE(edited.snapEdgeMidpoints, true);
    QVERIFY(edited.snapAngles
            == QVector<double>({0.0, 30.0, 45.0, 90.0, 180.0, 270.0}));

    findWidget<QPushButton>(dialog, "applyButton")->click();
    const AppSettings saved = store.load();
    verifySettingsEqual(saved, edited);

    dialog.setSettings(AppSettings{});
    QCOMPARE(findWidget<QSpinBox>(dialog, "snapPixels")->value(), 6);
    QVERIFY(findWidget<QCheckBox>(dialog, "snapAngle0")->isChecked());
    QVERIFY(findWidget<QCheckBox>(dialog, "snapAngle90")->isChecked());
}

void SettingsTests::workbenchDialogRoundTrip()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    SettingsStore store(temp.filePath(QStringLiteral("settings.ini")));
    SettingsDialog dialog(&store);

    QVERIFY(findWidget<QCheckBox>(dialog, "workbenchCacheEnabled")->isChecked());
    QCOMPARE(findWidget<QComboBox>(dialog, "workbenchScale")->currentData().toInt(), 50);
    QCOMPARE(findWidget<QSpinBox>(dialog, "workbenchExpandPercent")->value(), 50);
    QCOMPARE(findWidget<QSpinBox>(dialog, "workbenchDiskGb")->value(), 8);
    QCOMPARE(findWidget<QComboBox>(dialog, "workbenchWriterThreads")
                 ->currentData()
                 .toInt(),
             2);

    AppSettings expected;
    expected.workbenchCacheEnabled = false;
    expected.workbenchCacheScalePercent = 100;
    expected.workbenchCacheExpandPercent = 150;
    expected.workbenchCacheDiskBytes = 32 * kGiB;
    expected.workbenchCacheMemoryBytes = 512 * kMiB;
    expected.workbenchCacheWriterThreads = 3;

    dialog.setSettings(expected);
    QVERIFY(!findWidget<QCheckBox>(dialog, "workbenchCacheEnabled")->isChecked());
    QCOMPARE(findWidget<QComboBox>(dialog, "workbenchScale")->currentData().toInt(), 100);
    QCOMPARE(findWidget<QSpinBox>(dialog, "workbenchExpandPercent")->value(), 150);
    QCOMPARE(findWidget<QSpinBox>(dialog, "workbenchDiskGb")->value(), 32);
    QCOMPARE(findWidget<QComboBox>(dialog, "workbenchWriterThreads")
                 ->currentData()
                 .toInt(),
             3);
    verifySettingsEqual(dialog.settings(), expected);

    // Restoring the defaults puts the writer choice back to 2 threads.
    dialog.setSettings(AppSettings{});
    QCOMPARE(findWidget<QComboBox>(dialog, "workbenchWriterThreads")
                 ->currentData()
                 .toInt(),
             2);
}

void SettingsTests::workbenchDialogCancelDoesNotSave()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    SettingsStore store(temp.filePath(QStringLiteral("settings.ini")));
    SettingsDialog dialog(&store);

    findWidget<QCheckBox>(dialog, "workbenchCacheEnabled")->setChecked(false);
    auto* scale = findWidget<QComboBox>(dialog, "workbenchScale");
    scale->setCurrentIndex(scale->findData(25));
    findWidget<QSpinBox>(dialog, "workbenchDiskGb")->setValue(2);
    findWidget<QSpinBox>(dialog, "workbenchExpandPercent")->setValue(180);
    findWidget<QDialogButtonBox>(dialog, "settingsButtons")
        ->button(QDialogButtonBox::Cancel)
        ->click();

    QCOMPARE(dialog.result(), int(QDialog::Rejected));
    const AppSettings saved = store.load();
    QCOMPARE(saved.workbenchCacheEnabled, true);
    QCOMPARE(saved.workbenchCacheScalePercent, 50);
    QCOMPARE(saved.workbenchCacheExpandPercent, 50);
    QCOMPARE(saved.workbenchCacheDiskBytes, 8 * kGiB);
}

void SettingsTests::workbenchDialogApplySaves()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    SettingsStore store(temp.filePath(QStringLiteral("settings.ini")));
    SettingsDialog dialog(&store);

    findWidget<QCheckBox>(dialog, "workbenchCacheEnabled")->setChecked(false);
    auto* scale = findWidget<QComboBox>(dialog, "workbenchScale");
    scale->setCurrentIndex(scale->findData(25));
    findWidget<QSpinBox>(dialog, "workbenchDiskGb")->setValue(16);
    findWidget<QSpinBox>(dialog, "workbenchExpandPercent")->setValue(25);
    findWidget<QComboBox>(dialog, "workbenchWriterThreads")
        ->setCurrentIndex(0);
    findWidget<QPushButton>(dialog, "applyButton")->click();

    QCOMPARE(dialog.result(), 0);
    const AppSettings saved = store.load();
    QCOMPARE(saved.workbenchCacheEnabled, false);
    QCOMPARE(saved.workbenchCacheScalePercent, 25);
    QCOMPARE(saved.workbenchCacheExpandPercent, 25);
    QCOMPARE(saved.workbenchCacheDiskBytes, 16 * kGiB);
    QCOMPARE(saved.workbenchCacheMemoryBytes, 256 * kMiB);
    QCOMPARE(saved.workbenchCacheWriterThreads, 1);
}

void SettingsTests::workbenchDialogSnapsAndClamps()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    SettingsStore store(temp.filePath(QStringLiteral("settings.ini")));
    SettingsDialog dialog(&store);

    AppSettings invalid;
    invalid.workbenchCacheScalePercent = 60;
    invalid.workbenchCacheExpandPercent = -20;
    invalid.workbenchCacheDiskBytes = 0;
    invalid.workbenchCacheMemoryBytes = -5;
    dialog.setSettings(invalid);
    QCOMPARE(findWidget<QComboBox>(dialog, "workbenchScale")->currentData().toInt(), 50);
    QCOMPARE(findWidget<QSpinBox>(dialog, "workbenchExpandPercent")->value(), 0);
    QCOMPARE(findWidget<QSpinBox>(dialog, "workbenchDiskGb")->value(), 1);
    QCOMPARE(dialog.settings().workbenchCacheScalePercent, 50);
    QCOMPARE(dialog.settings().workbenchCacheExpandPercent, 0);
    QCOMPARE(dialog.settings().workbenchCacheDiskBytes, 1 * kGiB);
    QCOMPARE(dialog.settings().workbenchCacheMemoryBytes, 32 * kMiB);

    invalid.workbenchCacheScalePercent = 70;
    invalid.workbenchCacheExpandPercent = 500;
    invalid.workbenchCacheDiskBytes = 100 * kGiB;
    dialog.setSettings(invalid);
    QCOMPARE(findWidget<QComboBox>(dialog, "workbenchScale")->currentData().toInt(), 75);
    QCOMPARE(findWidget<QSpinBox>(dialog, "workbenchExpandPercent")->value(), 200);
    QCOMPARE(findWidget<QSpinBox>(dialog, "workbenchDiskGb")->value(), 64);
    QCOMPARE(dialog.settings().workbenchCacheDiskBytes, 64 * kGiB);
    QCOMPARE(dialog.settings().workbenchCacheExpandPercent, 200);
}

void SettingsTests::dialogGitCheckboxRoundTrip()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    SettingsStore store(temp.filePath(QStringLiteral("settings.ini")));
    SettingsDialog dialog(&store);

    auto* enabled = findWidget<QCheckBox>(dialog, "gitEnabled");
    QVERIFY(!enabled->isChecked());
    QVERIFY(!dialog.settings().gitEnabled);

    enabled->setChecked(true);
    QVERIFY(dialog.settings().gitEnabled);

    dialog.setSettings(AppSettings{});
    QVERIFY(!enabled->isChecked());

    AppSettings configured;
    configured.gitEnabled = true;
    configured.onboardingDone = true;
    dialog.setSettings(configured);
    QVERIFY(enabled->isChecked());
    QCOMPARE(dialog.settings().onboardingDone, true);

    findWidget<QPushButton>(dialog, "gitDetect")->click();
    QVERIFY(!findWidget<QLabel>(dialog, "gitStatus")->text().isEmpty());
}

void SettingsTests::dialogGitCancelDoesNotSave()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    SettingsStore store(temp.filePath(QStringLiteral("settings.ini")));
    SettingsDialog dialog(&store);

    findWidget<QCheckBox>(dialog, "gitEnabled")->setChecked(true);
    findWidget<QDialogButtonBox>(dialog, "settingsButtons")
        ->button(QDialogButtonBox::Cancel)
        ->click();

    QCOMPARE(dialog.result(), int(QDialog::Rejected));
    QCOMPARE(store.load().gitEnabled, false);
}

void SettingsTests::dialogGitApplySaves()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = temp.filePath(QStringLiteral("settings.ini"));
    SettingsStore store(path);

    AppSettings existing;
    existing.onboardingDone = true;
    store.save(existing);

    SettingsDialog dialog(&store);
    findWidget<QCheckBox>(dialog, "gitEnabled")->setChecked(true);
    findWidget<QPushButton>(dialog, "applyButton")->click();

    const AppSettings saved = store.load();
    QCOMPARE(saved.gitEnabled, true);
    QCOMPARE(saved.onboardingDone, true);
}

void SettingsTests::cancelDoesNotSave()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    SettingsStore store(temp.filePath(QStringLiteral("settings.ini")));
    SettingsDialog dialog(&store);

    findWidget<QSpinBox>(dialog, "htmlMemoryMb")->setValue(512);
    auto* buttons = findWidget<QDialogButtonBox>(dialog, "settingsButtons");
    buttons->button(QDialogButtonBox::Cancel)->click();

    QCOMPARE(dialog.result(), int(QDialog::Rejected));
    verifySettingsEqual(store.load(), AppSettings{});
}

void SettingsTests::applySavesWithoutClosing()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    SettingsStore store(temp.filePath(QStringLiteral("settings.ini")));
    SettingsDialog dialog(&store);

    findWidget<QSpinBox>(dialog, "htmlMemoryMb")->setValue(512);
    findWidget<QPushButton>(dialog, "applyButton")->click();

    QCOMPARE(dialog.result(), 0);
    QCOMPARE(store.load().htmlMemoryBytes, 512 * kMiB);

    findWidget<QDialogButtonBox>(dialog, "settingsButtons")
        ->button(QDialogButtonBox::Cancel)
        ->click();
}

void SettingsTests::okSavesAndAccepts()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    SettingsStore store(temp.filePath(QStringLiteral("settings.ini")));
    SettingsDialog dialog(&store);

    findWidget<QSpinBox>(dialog, "htmlDiskGb")->setValue(8);
    findWidget<QCheckBox>(dialog, "videoProxyEnabled")->setChecked(false);
    findWidget<QDialogButtonBox>(dialog, "settingsButtons")
        ->button(QDialogButtonBox::Ok)
        ->click();

    QCOMPARE(dialog.result(), int(QDialog::Accepted));
    const AppSettings settings = store.load();
    QCOMPARE(settings.htmlDiskBytes, 8 * kGiB);
    QCOMPARE(settings.videoProxyEnabled, false);
}

void SettingsTests::operationSignalsAreEmitted()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    SettingsStore store(temp.filePath(QStringLiteral("settings.ini")));
    SettingsDialog dialog(&store);

    QSignalSpy clearSpy(&dialog, &SettingsDialog::clearHtmlCacheRequested);
    QSignalSpy tokenSpy(&dialog, &SettingsDialog::resetMcpTokenRequested);
    QSignalSpy chromeSpy(&dialog, &SettingsDialog::chromeDetectRequested);

    findWidget<QPushButton>(dialog, "clearHtmlCache")->click();
    findWidget<QPushButton>(dialog, "resetMcpToken")->click();
    findWidget<QPushButton>(dialog, "chromeDetect")->click();

    QCOMPARE(clearSpy.count(), 1);
    QCOMPARE(tokenSpy.count(), 1);
    QCOMPARE(chromeSpy.count(), 1);
}

void SettingsTests::gitInfoAndEndpointPath()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    SettingsStore store(temp.filePath(QStringLiteral("settings.ini")));
    SettingsDialog dialog(&store);

    dialog.setGitInfo(QStringLiteral("/home/user/demo.pvproj"),
                      QStringLiteral("abc1234 pvf: human edit"));
    QCOMPARE(dialog.gitRepositoryPath(), QStringLiteral("/home/user/demo.pvproj"));
    QCOMPARE(dialog.gitRecentCommit(), QStringLiteral("abc1234 pvf: human edit"));
    QCOMPARE(findWidget<QLabel>(dialog, "gitRepositoryPath")->text(),
             QStringLiteral("/home/user/demo.pvproj"));
    QCOMPARE(findWidget<QLabel>(dialog, "gitRecentCommit")->text(),
             QStringLiteral("abc1234 pvf: human edit"));

    dialog.setGitInfo({}, {});
    QCOMPARE(dialog.gitRepositoryPath(), QString());
    QCOMPARE(findWidget<QLabel>(dialog, "gitRepositoryPath")->text(),
             QStringLiteral("（未打开工程）"));

    dialog.setEndpointPath(QStringLiteral("/tmp/demo/mcp-endpoint.json"));
    QCOMPARE(dialog.endpointPath(), QStringLiteral("/tmp/demo/mcp-endpoint.json"));
    QVERIFY(findWidget<QLineEdit>(dialog, "mcpEndpointPath")->isReadOnly());
}

void SettingsTests::onboardingDefaultsToUnchecked()
{
    OnboardingDialog dialog(AppSettings{});

    auto* enabled = findWidget<QCheckBox>(dialog, "onboardingGitEnabled");
    QVERIFY(!enabled->isChecked());
    QVERIFY(!findWidget<QLabel>(dialog, "onboardingGitStatus")->text().isEmpty());

    findWidget<QPushButton>(dialog, "onboardingGitDetect")->click();
    QVERIFY(!findWidget<QLabel>(dialog, "onboardingGitStatus")->text().isEmpty());

    findWidget<QPushButton>(dialog, "onboardingStart")->click();
    QCOMPARE(dialog.result(), int(QDialog::Accepted));
    QCOMPARE(dialog.gitEnabled(), false);
}

void SettingsTests::onboardingEnablesGit()
{
    OnboardingDialog dialog(AppSettings{});
    findWidget<QCheckBox>(dialog, "onboardingGitEnabled")->setChecked(true);
    findWidget<QPushButton>(dialog, "onboardingStart")->click();

    QCOMPARE(dialog.result(), int(QDialog::Accepted));
    QCOMPARE(dialog.gitEnabled(), true);
}

void SettingsTests::onboardingUsesConfiguredValue()
{
    AppSettings settings;
    settings.gitEnabled = true;
    OnboardingDialog dialog(settings);

    QVERIFY(findWidget<QCheckBox>(dialog, "onboardingGitEnabled")->isChecked());
    QCOMPARE(dialog.gitEnabled(), true);
}

QTEST_MAIN(SettingsTests)
#include "SettingsTests.moc"
