#include "html/HtmlLayerTest.h"

#include "app/MainWindow.h"
#include "core/Camera.h"
#include "core/Project.h"
#include "html/HtmlAsset.h"
#include "html/HtmlBrowser.h"
#include "html/HtmlRenderer.h"
#include "media/VideoDecoder.h"
#include "render/CameraRenderer.h"
#include "render/SceneRenderer.h"
#include "render/VideoExporter.h"

#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <cmath>
#include <functional>
#include <memory>

namespace pvf {
namespace html {

namespace {

QString progressHtml()
{
    return QStringLiteral(R"HTML(<!doctype html>
<html>
<head>
<meta charset="utf-8">
<style>
  html, body { margin: 0; padding: 0; width: 320px; height: 180px;
               background: transparent; overflow: hidden; }
  #track { position: absolute; left: 20px; top: 70px; width: 280px;
           height: 40px; background: #202020; }
  #bar { position: absolute; left: 0; top: 0; height: 40px; width: 0;
         background: #00e676; }
</style>
</head>
<body>
<div id="track"><div id="bar"></div></div>
<script>
(function () {
  'use strict';
  var bar = document.getElementById('bar');
  var last = null;
  function clamp(v) {
    v = Number(v);
    if (!isFinite(v)) v = 0;
    return Math.max(0, Math.min(100, v));
  }
  function render() {
    var vars = pvf.vars || {};
    var progress = vars.progress || {};
    var v = clamp(progress.value);
    if (v !== last) {
      last = v;
      bar.style.width = (v / 100 * 280).toFixed(2) + 'px';
    }
    pvf.rendered();
  }
  pvf.render = render;
  render();
  pvf.ready();
})();
</script>
</body>
</html>)HTML");
}

QByteArray progressAssetJson(bool withExtraComponent = false)
{
    const QJsonObject value{
        {QStringLiteral("name"), QStringLiteral("value")},
        {QStringLiteral("type"), QStringLiteral("number")},
        {QStringLiteral("min"), 0},
        {QStringLiteral("max"), 100},
        {QStringLiteral("default"), 0},
        {QStringLiteral("integer"), true},
    };
    const QJsonObject progress{
        {QStringLiteral("id"), QStringLiteral("progress")},
        {QStringLiteral("name"), QStringLiteral("Progress")},
        {QStringLiteral("variables"), QJsonArray{value}},
    };
    QJsonArray components{progress};
    if (withExtraComponent)
    {
        const QJsonObject extraValue{
            {QStringLiteral("name"), QStringLiteral("value")},
            {QStringLiteral("type"), QStringLiteral("number")},
            {QStringLiteral("min"), 0},
            {QStringLiteral("max"), 10},
            {QStringLiteral("default"), 7},
        };
        components.append(QJsonObject{
            {QStringLiteral("id"), QStringLiteral("extra")},
            {QStringLiteral("name"), QStringLiteral("Extra")},
            {QStringLiteral("variables"), QJsonArray{extraValue}},
        });
    }
    const QJsonObject manifest{
        {QStringLiteral("size"), QJsonArray{320, 180}},
        {QStringLiteral("transparent"), true},
        {QStringLiteral("timeDependent"), false},
        {QStringLiteral("components"), components},
    };
    const QJsonObject root{
        {QStringLiteral("format"), QStringLiteral("pvforge-html")},
        {QStringLiteral("version"), 1},
        {QStringLiteral("html"), progressHtml()},
        {QStringLiteral("manifest"), manifest},
    };
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

bool writeFile(const QString& path, const QByteArray& data)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

int greenPixels(const QImage& image)
{
    if (image.isNull())
        return 0;
    int count = 0;
    for (int y = 0; y < image.height(); y += 2)
        for (int x = 0; x < image.width(); x += 2)
        {
            const QColor color = image.pixelColor(x, y);
            if (color.green() > 180 && color.red() < 120 && color.blue() < 160)
                ++count;
        }
    return count;
}

bool waitUntil(const std::function<bool()>& predicate, int timeoutMs)
{
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < timeoutMs)
    {
        if (predicate())
            return true;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        QThread::msleep(5);
    }
    return predicate();
}

FrameRequest frameRequest(const HtmlAsset& asset, const Project& project,
                          EntityId id, int frame, const QSize& size)
{
    FrameRequest request;
    request.asset = asset;
    request.pageSize = size;
    request.frame = frame;
    request.time = frame / double(std::max(1, project.fps));
    request.timeoutMs = 3000;
    for (const HtmlComponent& component : asset.components)
        for (const HtmlVariable& variable : component.variables)
        {
            const QString key = component.id + QLatin1Char('.') + variable.name;
            request.vars.insert(
                key, project.value(id, QStringLiteral("var.") + key, frame));
        }
    return request;
}

} // namespace

int htmlLayerTest()
{
    int failures = 0;
    const QString executable = HtmlBrowser::detectExecutable();
    if (executable.isEmpty())
    {
        qInfo().noquote() << "html-layer-test: no Chrome/Edge/Chromium, skipped";
        return 0;
    }

    QTemporaryDir temp;
    if (!temp.isValid())
    {
        qCritical() << "html-layer-test: cannot create temp dir";
        return 1;
    }

    const QString sourcePath = temp.filePath(QStringLiteral("progress.json"));
    if (!writeFile(sourcePath, progressAssetJson()))
    {
        qCritical() << "html-layer-test: cannot write" << sourcePath;
        return 1;
    }

    auto* project = new Project;
    project->name = QStringLiteral("html-layer-test");
    project->filePath = temp.filePath(QStringLiteral("layer.pvproj"));
    project->packAssets = true;
    project->width = 320;
    project->height = 180;
    project->fps = 30;

    QString importError;
    const QString imported = project->importHtmlAsset(sourcePath, &importError);
    const QString assetsHtml = QFileInfo(project->filePath).absolutePath()
                               + QStringLiteral("/assets/html");
    if (imported.isEmpty() || !QFile::exists(imported)
        || !imported.startsWith(assetsHtml + QLatin1Char('/'))
        || !imported.endsWith(QStringLiteral(".json")))
    {
        qCritical() << "html-layer-test: import failed" << importError << imported;
        ++failures;
    }

    QString dataImportError;
    const QString secondAsset =
        project->importHtmlAssetData(progressAssetJson(true), &dataImportError);
    if (secondAsset.isEmpty() || !QFile::exists(secondAsset)
        || secondAsset == imported
        || !secondAsset.startsWith(assetsHtml + QLatin1Char('/')))
    {
        qCritical() << "html-layer-test: string import failed" << dataImportError
                    << secondAsset;
        ++failures;
    }

    Entity* layer = project->addHtmlLayer(imported);
    if (!layer)
    {
        qCritical() << "html-layer-test: addHtmlLayer failed";
        return 1;
    }
    const EntityId id = layer->id;
    if (layer->type != EntityType::HtmlLayer
        || layer->htmlLogicalSize() != QSize(320, 180)
        || layer->htmlTimeDependent()
        || std::abs(project->value(id, QStringLiteral("var.progress.value"), 0) - 0.0)
               > 1e-6)
    {
        qCritical() << "html-layer-test: entity defaults wrong" << int(layer->type)
                    << layer->htmlLogicalSize()
                    << project->value(id, QStringLiteral("var.progress.value"), 0);
        ++failures;
    }

    // Two keyframes: 10 -> 90 over 20 frames; the midpoint must evaluate.
    project->setValue(id, QStringLiteral("var.progress.value"), 0.0, 10.0, true);
    project->setValue(id, QStringLiteral("var.progress.value"), 20.0, 90.0, true);
    if (std::abs(project->value(id, QStringLiteral("var.progress.value"), 10) - 50.0)
        > 1e-6)
    {
        qCritical() << "html-layer-test: variable interpolation wrong";
        ++failures;
    }

    // Save/load keeps the layer, its asset path and the variable track.
    QString saveError;
    const QString projectPath = project->filePath;
    if (!project->save(projectPath, &saveError))
    {
        qCritical() << "html-layer-test: save failed" << saveError;
        return 1;
    }
    auto* loaded = new Project;
    if (!loaded->load(projectPath, &saveError))
    {
        qCritical() << "html-layer-test: load failed" << saveError;
        delete loaded;
        delete project;
        return 1;
    }
    delete project;
    const Entity* loadedLayer = loaded->entity(id);
    if (!loadedLayer || loadedLayer->type != EntityType::HtmlLayer
        || loadedLayer->htmlAssetPath() != imported
        || loadedLayer->htmlLogicalSize() != QSize(320, 180)
        || std::abs(loaded->value(id, QStringLiteral("var.progress.value"), 10) - 50.0)
               > 1e-6)
    {
        qCritical() << "html-layer-test: roundtrip lost HTML data";
        ++failures;
    }

    // A layer without a captured frame still draws a selectable placeholder
    // instead of leaving a hole (browser missing or first capture pending).
    {
        QImage canvas(320, 180, QImage::Format_ARGB32_Premultiplied);
        canvas.fill(Qt::transparent);
        QPainter painter(&canvas);
        QTransform worldToScreen;
        worldToScreen.translate(160, 90);
        SceneRenderer::drawEntities(painter, *loaded, 0, worldToScreen, false);
        painter.end();
        if (qAlpha(canvas.pixel(160, 90)) == 0)
        {
            qCritical() << "html-layer-test: placeholder was not drawn";
            ++failures;
        }
    }

    // MainWindow owns the browser and renderer and captures preview frames.
    MainWindow window(loaded);
    window.resize(1000, 700);
    window.show();
    window.goToFrame(20);

    const bool gotHi = waitUntil(
        [&] {
            const Entity* entity = loaded->entity(id);
            return entity && !entity->htmlImage.isNull()
                   && entity->htmlImage.size() == QSize(320, 180);
        },
        25000);
    const Entity* preview = loaded->entity(id);
    const int hiPixels = preview ? greenPixels(preview->htmlImage) : 0;
    if (!gotHi || hiPixels < 2000)
    {
        qCritical() << "html-layer-test: preview frame missing"
                    << (preview ? preview->htmlImage.size() : QSize())
                    << (preview ? preview->htmlError : QString()) << hiPixels;
        ++failures;
    }
    if (loadedLayer && (SceneRenderer::entityLocalBounds(*loadedLayer) != QRectF(-160, -90, 320, 180)))
    {
        qCritical() << "html-layer-test: local bounds"
                    << SceneRenderer::entityLocalBounds(*loadedLayer);
        ++failures;
    }

    // Changing the playhead changes pvf.vars and therefore the captured bar.
    window.goToFrame(0);
    const bool gotLo = waitUntil(
        [&] {
            const Entity* entity = loaded->entity(id);
            return entity && greenPixels(entity->htmlImage) < 800;
        },
        25000);
    if (!gotLo)
    {
        const Entity* entity = loaded->entity(id);
        qCritical() << "html-layer-test: variable change not captured"
                    << (entity ? greenPixels(entity->htmlImage) : -1)
                    << (entity ? entity->htmlError : QString());
        ++failures;
    }

    // Swapping the asset keeps same-named tracks and leaves orphan tracks in
    // place; the new manifest variable starts at its default.
    QString replaceError;
    if (!loaded->replaceHtmlAsset(id, secondAsset, &replaceError))
    {
        qCritical() << "html-layer-test: replace failed" << replaceError;
        ++failures;
    }
    else
    {
        const Entity* replaced = loaded->entity(id);
        const bool trackKept = replaced
                               && replaced->tracks.contains(
                                   QStringLiteral("var.progress.value"));
        const double extra =
            loaded->value(id, QStringLiteral("var.extra.value"), 0);
        if (!trackKept || std::abs(extra - 7.0) > 1e-6)
        {
            qCritical() << "html-layer-test: replace did not keep old track / new default"
                        << trackKept << extra;
            ++failures;
        }
    }

    qInfo() << "html-layer-test failures" << failures;
    return failures == 0 ? 0 : 1;
}

int htmlExportTest()
{
    int failures = 0;
    if (VideoExporter::ffmpegExecutable().isEmpty())
    {
        qInfo().noquote() << "html-export-test: ffmpeg missing, skipped";
        return 0;
    }
    if (HtmlBrowser::detectExecutable().isEmpty())
    {
        qInfo().noquote() << "html-export-test: no Chrome/Edge/Chromium, skipped";
        return 0;
    }

    QTemporaryDir temp;
    if (!temp.isValid())
    {
        qCritical() << "html-export-test: cannot create temp dir";
        return 1;
    }
    const QString sourcePath = temp.filePath(QStringLiteral("progress.json"));
    if (!writeFile(sourcePath, progressAssetJson()))
    {
        qCritical() << "html-export-test: cannot write asset";
        return 1;
    }

    Project project;
    project.name = QStringLiteral("html-export-test");
    project.width = 320;
    project.height = 180;
    project.fps = 24;
    project.background = QColor(10, 20, 40);
    const QString imported = project.importHtmlAsset(sourcePath, nullptr);
    if (imported.isEmpty())
    {
        qCritical() << "html-export-test: import failed";
        return 1;
    }
    Entity* layer = project.addHtmlLayer(imported);
    if (!layer)
    {
        qCritical() << "html-export-test: addHtmlLayer failed";
        return 1;
    }
    const EntityId id = layer->id;
    project.setValue(id, QStringLiteral("var.progress.value"), 0.0, 10.0, true);
    project.setValue(id, QStringLiteral("var.progress.value"), 7.0, 100.0, true);

    QString parseError;
    const HtmlAsset asset = HtmlAsset::parseFile(imported, &parseError);
    if (!parseError.isEmpty())
    {
        qCritical() << "html-export-test: asset parse failed" << parseError;
        return 1;
    }

    HtmlBrowser browser;
    QString startError;
    if (!browser.start(QString(), &startError))
    {
        qCritical() << "html-export-test: browser start failed" << startError;
        return 1;
    }
    HtmlRenderer renderer(&browser);

    const QString outputPath = temp.filePath(QStringLiteral("html.mp4"));
    ExportOptions options;
    options.outputPath = outputPath;
    options.size = QSize(320, 180);
    options.fps = 24;
    options.frameCount = 8;
    options.crf = 20;
    options.includeAudio = false;

    auto provider = [&project, &asset, &renderer, id](
                        int frame, VideoExporter::FrameDone done) {
        const FrameRequest request =
            frameRequest(asset, project, id, frame, QSize(320, 180));
        renderer.requestFrame(
            request, [&project, id, frame, done](const QImage& image,
                                                 const QString& error) {
                if (image.isNull())
                {
                    qWarning() << "html-export-test: capture failed" << error;
                    done(QImage());
                    return;
                }
                Entity* entity = project.entity(id);
                if (entity)
                {
                    entity->htmlImage = image;
                    entity->pixmap = QPixmap();
                    entity->thumbnail = QImage();
                }
                Camera::State state;
                state.scale = 1.0;
                state.size = QSizeF(project.width, project.height);
                done(CameraRenderer::renderCameraImage(
                    project, frame, state, QSize(320, 180), false));
            });
    };

    VideoExporter exporter;
    bool reported = false;
    bool success = false;
    QString exportError;
    int callbacks = 0;
    QEventLoop loop;
    QObject::connect(&exporter, &VideoExporter::finished, &loop,
                     [&](bool ok, const QString& error) {
                         reported = true;
                         success = ok;
                         exportError = error;
                         loop.quit();
                     });
    QObject::connect(&exporter, &VideoExporter::progress, &loop,
                     [&callbacks](int done, int) { callbacks = done; });
    QTimer guard;
    guard.setSingleShot(true);
    QObject::connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);

    QString error;
    if (!exporter.start(project, options, provider, &error))
    {
        qCritical() << "html-export-test: start failed" << error;
        return 1;
    }
    guard.start(90000);
    loop.exec();

    if (!reported || !success || exporter.isRunning() || callbacks != 8)
    {
        qCritical() << "html-export-test: export failed" << reported << success
                    << exportError << callbacks;
        ++failures;
    }

    QString decodeError;
    double duration = 0.0;
    const QImage first = decodeVideoFirstFrame(outputPath, &decodeError, &duration);
    if (first.isNull() || first.size() != QSize(320, 180)
        || greenPixels(first) < 10 || !QFile::exists(outputPath))
    {
        qCritical() << "html-export-test: first frame check failed" << decodeError
                    << first.size() << greenPixels(first) << duration;
        ++failures;
    }

    qInfo() << "html-export-test failures" << failures;
    return failures == 0 ? 0 : 1;
}

} // namespace html
} // namespace pvf
