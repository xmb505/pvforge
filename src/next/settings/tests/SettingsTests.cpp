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
    QCOMPARE(int(actual.mcpPort), int(expected.mcpPort));
    QCOMPARE(actual.humanCommitDelayMs, expected.humanCommitDelayMs);
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

    void detectGitFindsExecutable();

    void dialogDefaults();
    void dialogRoundTrip();
    void dialogClampsValues();
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

void SettingsTests::dialogDefaults()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    SettingsStore store(temp.filePath(QStringLiteral("settings.ini")));
    SettingsDialog dialog(&store);

    verifySettingsEqual(dialog.settings(), AppSettings{});
    QCOMPARE(findWidget<QTabWidget>(dialog, "settingsTabs")->count(), 4);
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
