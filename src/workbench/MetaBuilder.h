#pragma once

#include "assets/AssetStore.h"
#include "model/Model.h"

#include <QByteArray>
#include <QRectF>
#include <QSize>
#include <QString>

namespace pvf::workbench {

// Settings shared by the frame cache and its metadata.  targetSize is the
// actual cached image size; callers derive it with cacheSizeFor().
//
// expandPercent applies only to the editor canvas view: the cached world
// rectangle covers the canvas plus that percentage of its width/height on
// every side.  Camera views keep it at 0.  transparentOutsideCanvas stores the
// expanded area as transparent pixels instead of filling it with the project
// background; it is meaningful only when expandPercent > 0.
struct Options
{
    int scalePercent = 50;
    int maxWidth = 1280;
    int expandPercent = 50;
    bool transparentOutsideCanvas = false;
    QSize targetSize;
    qint64 diskBudgetBytes = 8LL * 1024 * 1024 * 1024;
    qint64 memoryBudgetBytes = 256LL * 1024 * 1024;
    QString rendererVersion = QStringLiteral("1");
    QString chromeVersion; // part of the signature when the project has HTML
};

// Deterministic serialisation of everything that affects one rendered frame.
// bytes are compared only after sha1 to keep the hot playback path cheap.
struct FrameMeta
{
    QByteArray bytes;
    QByteArray sha1; // 20 bytes

    bool equals(const FrameMeta& other) const;
};

// Builds FrameMeta without modifying the project.  Only entities active at the
// requested frame and their parent chain are serialised, sorted by UUID.
class MetaBuilder
{
public:
    MetaBuilder(const model::Project& project, const assets::AssetStore& assets);

    FrameMeta build(int frame, const Uuid& cameraUuid, const Options& options) const;

private:
    const model::Project& m_project;
    const assets::AssetStore& m_assets;
};

// Project-wide cache directory key: renderer version, scale, expand percent,
// transparent-outside mode, fps, project size, plus the Chrome version when the
// project contains HTML.  Returns a filesystem-safe string with a stable
// prefix and a SHA-1 digest.
QString renderSignature(const model::Project& project, const Options& options);

// World rectangle rendered by the workbench cache: the project canvas
// (0,0,width,height) expanded by width*percent/100 on the left/right and
// height*percent/100 on the top/bottom.  percent is clamped to 0..200.
QRectF renderRect(const model::Project& project, int expandPercent);

// Size of renderRect() at the requested scale, clamped to maxWidth while
// keeping the aspect ratio.  maxWidth <= 0 means no clamp.  The default
// expandPercent = 0 keeps the historical canvas-only behaviour.
QSize cacheSizeFor(const model::Project& project, int scalePercent, int maxWidth,
                   int expandPercent = 0);

} // namespace pvf::workbench
