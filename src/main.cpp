#include "app/MainWindow.h"
#include "core/Project.h"
#include "media/VideoDecoder.h"
#include "ui/TimelinePanel.h"
#include "ui/Viewport.h"
#include "ui/WorkbenchPanel.h"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QImage>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
#include <cmath>

namespace {

int selfTest()
{
    pvf::Project project;

    QImage img(64, 64, QImage::Format_ARGB32);
    img.fill(Qt::red);
    const QString path = QDir::tempPath() + QStringLiteral("/pvforge_selftest.png");
    if (!img.save(path))
    {
        qCritical() << "self-test: failed to write" << path;
        return 1;
    }

    pvf::Entity* e = project.addImage(path);
    if (!e)
    {
        qCritical() << "self-test: addImage failed";
        return 1;
    }
    const pvf::EntityId id = e->id;

    project.setValue(id, QStringLiteral("x"), 0, 0, true);
    project.setValue(id, QStringLiteral("x"), 30, 600, true);

    const double mid = project.value(id, QStringLiteral("x"), 15);
    if (std::abs(mid - 300.0) > 1e-6)
    {
        qCritical() << "self-test: interpolation expected 300, got" << mid;
        return 1;
    }
    if (std::abs(project.value(id, QStringLiteral("x"), 0) - 0.0) > 1e-6
        || std::abs(project.value(id, QStringLiteral("x"), 30) - 600.0) > 1e-6)
    {
        qCritical() << "self-test: keyframe endpoints wrong";
        return 1;
    }
    if (std::abs(project.value(id, QStringLiteral("opacity"), 0) - 1.0) > 1e-6)
    {
        qCritical() << "self-test: opacity default wrong";
        return 1;
    }

    QFile::remove(path);
    qInfo() << "self-test OK: keyframe interpolation x(15) =" << mid;
    return 0;
}

} // namespace

namespace {

int renderDemo(QApplication& app)
{
    const QString assetPath = QDir::tempPath() + QStringLiteral("/pvforge_demo_asset.png");

    QImage asset(400, 400, QImage::Format_ARGB32);
    asset.fill(Qt::transparent);
    {
        QPainter p(&asset);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(90, 170, 255));
        p.drawRoundedRect(20, 20, 360, 360, 48, 48);
        p.setBrush(QColor(255, 255, 255));
        p.drawEllipse(120, 90, 160, 160);
        p.setBrush(QColor(20, 30, 50));
        p.drawEllipse(160, 130, 80, 80);
        p.setPen(QPen(QColor(255, 255, 255), 6));
        QFont f = p.font();
        f.setPointSize(36);
        f.setBold(true);
        p.setFont(f);
        p.drawText(QRect(0, 280, 400, 80), Qt::AlignCenter, QStringLiteral("PV"));
    }
    if (!asset.save(assetPath))
    {
        qCritical() << "demo: failed to save asset";
        return 1;
    }

    pvf::MainWindow window;
    window.resize(1440, 900);
    window.show();

    const pvf::EntityId id = window.importImageFile(assetPath);
    if (id == pvf::kInvalidEntity)
    {
        qCritical() << "demo: import failed";
        return 1;
    }

    auto* project = window.project();
    project->setValue(id, QStringLiteral("x"), 0, -500, true);
    project->setValue(id, QStringLiteral("x"), 30, 500, true);
    project->setValue(id, QStringLiteral("rotation"), 0, -20, true);
    project->setValue(id, QStringLiteral("rotation"), 30, 20, true);
    project->setValue(id, QStringLiteral("opacity"), 0, 0.25, true);
    project->setValue(id, QStringLiteral("opacity"), 30, 1.0, true);

    QTimer::singleShot(400, &window, [&app, &window] {
        window.goToFrame(0);
        const QImage f0 = window.grab().toImage().convertToFormat(QImage::Format_ARGB32);
        window.goToFrame(15);
        const QImage f15 = window.grab().toImage().convertToFormat(QImage::Format_ARGB32);

        f0.save(QStringLiteral("/tmp/pvforge_demo_f0.png"));
        f15.save(QStringLiteral("/tmp/pvforge_demo_f15.png"));

        int diff = 0;
        if (f0.size() == f15.size())
        {
            for (int y = 0; y < f0.height(); ++y)
            {
                const auto* a = reinterpret_cast<const QRgb*>(f0.constScanLine(y));
                const auto* b = reinterpret_cast<const QRgb*>(f15.constScanLine(y));
                for (int x = 0; x < f0.width(); ++x)
                {
                    if (std::abs(qRed(a[x]) - qRed(b[x])) + std::abs(qGreen(a[x]) - qGreen(b[x]))
                            + std::abs(qBlue(a[x]) - qBlue(b[x]))
                        > 40)
                        ++diff;
                }
            }
        }

        qInfo() << "demo: differing pixels between frame 0 and 15:" << diff;
        app.exit(diff > 1000 ? 0 : 1);
    });

    return app.exec();
}

} // namespace

namespace {

int playTest(QApplication& app)
{
    QImage img(64, 64, QImage::Format_ARGB32);
    img.fill(Qt::red);
    const QString path = QDir::tempPath() + QStringLiteral("/pvforge_playtest.png");
    if (!img.save(path))
        return 1;

    pvf::MainWindow window;
    window.resize(1200, 800);
    window.show();

    const pvf::EntityId id = window.importImageFile(path);
    if (id == pvf::kInvalidEntity)
        return 1;
    window.project()->setValue(id, QStringLiteral("x"), 0, 0, true);
    window.project()->setValue(id, QStringLiteral("x"), 60, 600, true);

    window.goToFrame(0);
    window.togglePlayback();

    QTimer::singleShot(600, &window, [&app, &window] {
        const int frame = window.currentFrame();
        qInfo() << "play-test: frame after 600ms =" << frame;
        app.exit(frame >= 10 ? 0 : 1);
    });
    return app.exec();
}

int layoutTest(QApplication& app)
{
    pvf::MainWindow window;
    window.resize(480, 1060);
    window.show();

    QTimer::singleShot(300, &window, [&app, &window] {
        window.grab().toImage().save(QStringLiteral("/tmp/pvforge_layout_narrow.png"));
        qInfo() << "layout-test: size" << window.size() << "minimum" << window.minimumSize();
        if (auto* timeline = window.findChild<pvf::TimelinePanel*>())
            qInfo() << "timeline geometry" << timeline->geometry()
                    << "min" << timeline->minimumSize();
        if (auto* viewport = window.findChild<pvf::Viewport*>())
            qInfo() << "viewport geometry" << viewport->geometry()
                    << "min" << viewport->minimumSize();
        app.exit(0);
    });
    return app.exec();
}

int workbenchTest(QApplication& app)
{
    QString error;
    const QImage videoFrame =
        pvf::decodeVideoFirstFrame(QStringLiteral("/tmp/pvf_test.mp4"), &error);
    if (videoFrame.isNull())
    {
        qCritical() << "workbench-test: video decode failed:" << error;
        return 1;
    }
    qInfo() << "workbench-test: video first frame" << videoFrame.size();

    pvf::MainWindow window;
    window.resize(1440, 900);
    window.show();

    auto* project = window.project();

    pvf::Entity* circle = project->addVector(
        1, 220, 220, QColor(255, 120, 160, 200), QColor(255, 255, 255), 8);
    project->setValue(circle->id, QStringLiteral("x"), 0, 300, false);
    project->setValue(circle->id, QStringLiteral("y"), 0, -200, false);

    pvf::Entity* video = project->addVideo(QStringLiteral("/tmp/pvf_test.mp4"), videoFrame);
    project->setValue(video->id, QStringLiteral("x"), 0, -350, false);
    project->setValue(video->id, QStringLiteral("y"), 0, -200, false);
    project->setValue(video->id, QStringLiteral("scaleX"), 0, 0.6, false);
    project->setValue(video->id, QStringLiteral("scaleY"), 0, 0.6, false);

    auto* button = window.findChild<QToolButton*>(QStringLiteral("workbenchButton"));
    auto* workbench = window.findChild<pvf::WorkbenchPanel*>();
    if (!button || !workbench)
    {
        qCritical() << "workbench-test: UI objects not found";
        return 1;
    }
    button->click();

    QTimer::singleShot(450, &window, [&app, &window, workbench, button] {
        window.grab().toImage().save(QStringLiteral("/tmp/pvforge_workbench.png"));

        auto* types = workbench->findChild<QListWidget*>(QStringLiteral("typeList"));
        auto* content = workbench->findChild<QLineEdit*>(QStringLiteral("textContent"));
        auto* create = workbench->findChild<QPushButton*>(QStringLiteral("createButton"));
        if (!types || !content || !create)
        {
            qCritical() << "workbench-test: form widgets not found";
            app.exit(1);
            return;
        }

        types->setCurrentRow(2);
        content->setText(QStringLiteral("工作台创建的文字"));
        create->click();

        QTimer::singleShot(350, &window, [&app, &window, button, workbench] {
            window.grab().toImage().save(QStringLiteral("/tmp/pvforge_types.png"));
            const int count = window.project()->entities().size();
            qInfo() << "workbench-test: entities after text create =" << count;

            button->click();
            auto* typeList =
                workbench->findChild<QListWidget*>(QStringLiteral("typeList"));
            typeList->setCurrentRow(3);   // vector page

            QTimer::singleShot(300, &window, [&app, &window, workbench, count,
                                              typeList] {
                window.grab().toImage().save(
                    QStringLiteral("/tmp/pvforge_workbench_vector.png"));
                auto* createButton =
                    workbench->findChild<QPushButton*>(QStringLiteral("createButton"));
                createButton->click();

                QTimer::singleShot(250, &window, [&app, &window, count] {
                    window.grab().toImage().save(QStringLiteral("/tmp/pvforge_vector.png"));
                    const int after = window.project()->entities().size();
                    qInfo() << "workbench-test: entities after vector create =" << after;
                    app.exit(after == count + 1 ? 0 : 1);
                });
            });
        });
    });

    return app.exec();
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("PVForge"));
    app.setApplicationDisplayName(QStringLiteral("PVForge"));
    app.setOrganizationName(QStringLiteral("PVForge"));

    if (app.arguments().contains(QStringLiteral("--self-test")))
        return selfTest();
    if (app.arguments().contains(QStringLiteral("--demo-shot")))
        return renderDemo(app);
    if (app.arguments().contains(QStringLiteral("--play-test")))
        return playTest(app);
    if (app.arguments().contains(QStringLiteral("--workbench-test")))
        return workbenchTest(app);
    if (app.arguments().contains(QStringLiteral("--layout-test")))
        return layoutTest(app);

    pvf::MainWindow window;
    window.show();
    return app.exec();
}
