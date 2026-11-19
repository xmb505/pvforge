// Lifecycle/concurrency tests for HtmlFrameService (issue #126).
//
// A dedicated executable: these cases never launch Chrome (they point the
// service at a missing executable), so they are fast and deterministic, and
// Html2Tests.cpp stays inside the code-size ratchet.

#include "html2/HtmlFrameService.h"

#include <QDir>
#include <QImage>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QtTest>

#include <thread>

using namespace pvf;

class HtmlConcurrencyTest : public QObject
{
    Q_OBJECT

private slots:
    void startStopFromTwoThreads()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());

        for (int iteration = 0; iteration < 10; ++iteration)
        {
            const QString cache =
                QDir(root.path()).filePath(QStringLiteral("html-%1").arg(iteration));
            html2::HtmlFrameService service(cache, 1 << 20, 1 << 20);
            service.setChromeExecutable(QStringLiteral("/nonexistent/chrome"));

            std::thread starter([&service] {
                QString error;
                service.start(&error);
            });
            std::thread stopper([&service] { service.stop(); });
            starter.join();
            stopper.join();

            QVERIFY(!service.isRunning());
        }
    }

    void finalOnlyNeverStartsPreviewBrowsers()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        html2::HtmlFrameService service(
            QDir(root.path()).filePath(QStringLiteral("export")), 1 << 20,
            1 << 20);
        service.setChromeExecutable(QStringLiteral("/nonexistent/chrome"));
        service.setFinalOnly(true);

        html2::FrameRequest request;
        request.bundleDir = root.path();
        request.size = QSize(64, 64);
        request.timeoutMs = 500;

        QString error;
        const QImage image = service.finalFrame(request, &error);
        QVERIFY(image.isNull());
        QVERIFY(!error.isEmpty());
        // Only the final renderer may exist; the frontend/cache service stays
        // down, so isRunning() (which reflects the preview browsers) is false.
        QVERIFY(!service.isRunning());
        service.stopFinal();
    }
};

QTEST_MAIN(HtmlConcurrencyTest)
#include "HtmlConcurrencyTests.moc"
