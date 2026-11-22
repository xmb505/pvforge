#include "next/settings/GitDetect.h"
#include "next/settings/OnboardingDialog.h"
#include "next/settings/SettingsStore.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
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
    QCOMPARE(actual.audioScrub, expected.audioScrub);
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
    expected.audioScrub = false;
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
    QVERIFY(raw.contains(QStringLiteral("editor/audioScrub")));
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
    expected.workbenchCacheWriterThreads = 4;
    store.save(expected);

    SettingsStore reopened(path);
    const AppSettings loaded = reopened.load();
    QCOMPARE(loaded.workbenchCacheEnabled, false);
    QCOMPARE(loaded.workbenchCacheScalePercent, 75);
    QCOMPARE(loaded.workbenchCacheExpandPercent, 125);
    QCOMPARE(loaded.workbenchCacheDiskBytes, 24 * kGiB);
    QCOMPARE(loaded.workbenchCacheMemoryBytes, 512 * kMiB);
    QCOMPARE(loaded.workbenchCacheWriterThreads, 4);

    QSettings raw(path, QSettings::IniFormat);
    QCOMPARE(raw.value(QStringLiteral("workbench/enabled")).toBool(), false);
    QCOMPARE(raw.value(QStringLiteral("workbench/scalePercent")).toInt(), 75);
    QCOMPARE(raw.value(QStringLiteral("workbench/expandPercent")).toInt(), 125);
    QCOMPARE(raw.value(QStringLiteral("workbench/diskBytes")).toLongLong(), 24 * kGiB);
    QCOMPARE(raw.value(QStringLiteral("workbench/memoryBytes")).toLongLong(), 512 * kMiB);
    QCOMPARE(raw.value(QStringLiteral("workbench/writerThreads")).toInt(), 4);
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

    // Writer threads are a hard 1..4 choice, not a snap: hand-edited INI
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
    QCOMPARE(threads(4), 4);
    QCOMPARE(threads(9), 4);
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
