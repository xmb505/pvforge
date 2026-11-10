#pragma once

#include "assets/AssetStore.h"
#include "model/Model.h"

#include <QByteArray>
#include <QSize>
#include <QString>

namespace pvf::workbench {

// Settings shared by the frame cache and its metadata.  targetSize is the
// actual cached image size; callers derive it with cacheSizeFor().
struct Options
{
    int scalePercent = 50;
    int maxWidth = 1280;
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

// Project-wide cache directory key: renderer version, scale, fps, project
// size, plus the Chrome version when the project contains HTML.  Returns a
// filesystem-safe string with a stable prefix and a SHA-1 digest.
QString renderSignature(const model::Project& project, const Options& options);

// Project canvas size at the requested scale, clamped to maxWidth while
// keeping the aspect ratio.  maxWidth <= 0 means no clamp.
QSize cacheSizeFor(const model::Project& project, int scalePercent, int maxWidth);

} // namespace pvf::workbench
