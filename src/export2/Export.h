#pragma once

#include "assets/AssetStore.h"
#include "common/Types.h"
#include "model/Project.h"
#include "render2/Render.h"

#include <QSize>
#include <QString>
#include <QStringList>

#include <functional>

namespace pvf::export2 {

// Called once after each frame has been encoded.  `frame` is 1-based, `total`
// is the number of frames in the export, and `etaSeconds` is the estimated
// remaining time based on the average time per finished output frame (never
// negative, never increasing, exactly 0 on the last frame).  Returning false
// cancels the export: the temporary file is removed and Result::error is
// "cancelled".
using Progress = std::function<bool(int frame, int total, double etaSeconds)>;

// Legacy two-argument progress callback.  Existing callers keep working; they
// simply do not receive the ETA.  The frame/total semantics are unchanged.
using LegacyProgress = std::function<bool(int frame, int total)>;

// Options for one synchronous MP4 export.
//
// The camera is mandatory: `cameraUuid` must name an entity whose content kind
// is Camera.  The default frame range is the camera's own [start, end).
//
// `startFrame` / `endFrame` override that range only when they describe a
// non-empty range (`endFrame > startFrame`); otherwise the camera range is
// used.  An explicit range must stay inside the camera range, otherwise the
// export fails instead of silently clamping.
//
// An empty `outputSize` means the project canvas size.  The size must be
// within 1..8192 on both axes; odd dimensions are rounded down to even because
// H.264 yuv420p needs even width/height.  When the requested aspect ratio does
// not match the project, the project canvas is scaled to fit and centered
// (letterbox/pillarbox), with the project background colour filling the bars.
//
// `outputFps` is the frame rate of the exported MP4 and must be 1x/2x/4x the
// project fps (0 = project fps).  Each output frame samples the project at
// `outputFrame * projectFps / outputFps`, so keyframes, video and HTML are
// time-sampled while the project data stays untouched.  `fps` is a legacy alias
// used only when `outputFps` is 0.  `crf` is the quality factor: 0..51 is
// accepted (lower = better), anything else fails with "invalid_crf".
//
// `encoder` selects the H.264 implementation: "libx264" (software, the
// default) or "h264_vaapi" (hardware).  VAAPI converts every frame to NV12 and
// uploads it to the device; when the FFmpeg build has no h264_vaapi encoder or
// no usable VAAPI device the export fails with "encoder_unavailable" instead of
// crashing.  `preset` is the libx264 speed preset, one of
// ultrafast/superfast/veryfast/faster/fast/medium/slow/slower/veryslow;
// unknown names fail with "unknown_preset".
//
// `resolutionScale` is 1, 2 or 4: without an explicit `outputSize`, the export
// is the project canvas multiplied by that integer factor (aspect preserved,
// camera mapping scales with it).  An explicit `outputSize` wins over the
// scale.
//
// Audio: every Asset entity with assetType Audio that overlaps the export
// range and is visible during that overlap is decoded through FFmpeg,
// resampled to 48 kHz stereo float and mixed additively.  Visibility follows
// the Anim rule: the step-evaluated `visible` track and inherited parent
// visibility; editor-only `hidden` is ignored.  Invisible stretches of a
// partially visible entity stay silent.  The
// entity's immutable `content.asset.volume` is applied; a `volume` keyframe
// track overrides it per decoded block.  The mix is encoded as AAC
// (192 kbit/s) and muxed into the MP4 when at least one audio entity was
// usable.  Missing/unsupported audio assets are skipped with a warning, and a
// project with no usable audio still exports as a video-only MP4.  Set
// `includeAudio = false` to force a video-only export.
struct Options
{
    QString outputPath; // .mp4
    Uuid cameraUuid;    // required
    int startFrame = 0; // ignored unless endFrame > startFrame
    int endFrame = 0;   // exclusive
    QSize outputSize;   // empty = project size * resolutionScale
    int fps = 0;        // legacy alias of outputFps; 0 = project fps
    int outputFps = 0;  // 0 = project fps; otherwise 1x/2x/4x project fps
    int resolutionScale = 1; // 1, 2 or 4
    int crf = 18;            // 0..51
    QString encoder = QStringLiteral("libx264"); // libx264 | h264_vaapi
    QString preset = QStringLiteral("medium"); // libx264 speed preset
    bool includeAudio = true;
    // When false (default), an existing output file makes the export fail with
    // error "output_exists" and the file is left untouched.  Set true only
    // after the user confirmed overwriting.
    bool overwrite = false;

    // Optional progress callback; the explicit exportVideo() argument wins
    // when both are set.  See the Progress type above.
    Progress progress;

    // Optional decoded-frame sources for video and HTML assets, forwarded to
    // render2::Options for every frame.  Unset providers render placeholders.
    render2::FrameProvider videoFrame;
    render2::FrameProvider htmlFrame;
};

struct Result
{
    bool ok = false;
    QString error;
    int framesWritten = 0;
    // Non-fatal audio problems (missing asset, undecodable file, no AAC
    // encoder, ...).  The export still succeeds and falls back to silence.
    QStringList warnings;
};

// Renders the project through render2 with the selected camera and encodes the
// frames as H.264/yuv420p into an MP4.  The file is written to a temporary
// sibling first and atomically renamed on success, so a failed or cancelled
// export never leaves a partial .mp4 behind.
//
// The explicit `progress` argument takes precedence over Options::progress.
Result exportVideo(const model::Project& project,
                   const assets::AssetStore& assets, const Options& options,
                   Progress progress = {});

// Backwards-compatible overload for two-argument callbacks.
Result exportVideo(const model::Project& project,
                   const assets::AssetStore& assets, const Options& options,
                   LegacyProgress progress);

} // namespace pvf::export2
