#include "mcp2/Internal.h"

#include "anim/Anim.h"
#include "render2/Render.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonObject>
#include <QStandardPaths>

#include <algorithm>

namespace pvf::mcp2::detail {

namespace {

constexpr int kMaxPreviewSide = 4096;
constexpr qint64 kMaxPreviewBytes = 20LL * 1024LL * 1024LL;

QJsonValue uuidValue(const Uuid& uuid)
{
    return uuid.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(uuid);
}

QJsonValue viewRectValue(const QRect& rect)
{
    if (rect.isEmpty())
        return QJsonValue(QJsonValue::Null);
    QJsonObject object;
    object.insert(QStringLiteral("x"), rect.x());
    object.insert(QStringLiteral("y"), rect.y());
    object.insert(QStringLiteral("width"), rect.width());
    object.insert(QStringLiteral("height"), rect.height());
    return object;
}

void insertViewState(QJsonObject* data, const ViewState& state)
{
    data->insert(QStringLiteral("playhead"), state.playhead);
    data->insert(QStringLiteral("selection"), uuidValue(state.selection));
    data->insert(QStringLiteral("timelineParent"),
                 uuidValue(state.timelineParent));
    data->insert(QStringLiteral("cameraUuid"), uuidValue(state.cameraUuid));
    data->insert(QStringLiteral("viewRect"), viewRectValue(state.viewRect));
}

} // namespace

Result viewStateGet(Tx& tx, const QJsonObject& args)
{
    Q_UNUSED(args);
    const model::Project& project = tx.p();

    QJsonObject data;
    insertViewState(&data, tx.viewState);
    data.insert(QStringLiteral("fps"), project.settings.fps);
    data.insert(QStringLiteral("width"), project.settings.width);
    data.insert(QStringLiteral("height"), project.settings.height);
    data.insert(QStringLiteral("root"), tx.store->root());

    QJsonObject summary;
    summary.insert(QStringLiteral("fps"), project.settings.fps);
    summary.insert(QStringLiteral("width"), project.settings.width);
    summary.insert(QStringLiteral("height"), project.settings.height);
    summary.insert(QStringLiteral("root"), tx.store->root());
    data.insert(QStringLiteral("project"), summary);
    return success(data);
}

Result previewRenderFrame(Tx& tx, const QJsonObject& args)
{
    static const QStringList allowed = {
        QStringLiteral("frame"),      QStringLiteral("cameraUuid"),
        QStringLiteral("width"),      QStringLiteral("height"),
        QStringLiteral("background"),
    };
    for (auto it = args.constBegin(); it != args.constEnd(); ++it)
    {
        if (!allowed.contains(it.key()))
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("未知参数 %1").arg(it.key()));
    }

    Result error;
    const model::Project& project = tx.p();

    int frame = tx.viewState.playhead;
    if (!readInt(args, "frame", &frame, &error))
        return error;
    if (frame < 0 || frame > 1000000)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("frame 必须在 0..1000000"));

    Uuid cameraUuid = tx.viewState.cameraUuid;
    if (!readUuid(args, "cameraUuid", &cameraUuid, &error, false, true))
        return error;

    const auto dimension = [&](const char* key, int fallback, int* out) {
        *out = std::clamp(fallback, 1, kMaxPreviewSide);
        int value = *out;
        if (!readInt(args, key, &value, &error))
            return false;
        if (args.contains(QLatin1String(key)) && (value < 1 || value > kMaxPreviewSide))
        {
            error = failure(
                QStringLiteral("invalid_argument"),
                QStringLiteral("%1 必须在 1..%2")
                    .arg(QLatin1String(key))
                    .arg(kMaxPreviewSide));
            return false;
        }
        *out = value;
        return true;
    };

    int width = 0;
    int height = 0;
    if (!dimension("width", project.settings.width, &width)
        || !dimension("height", project.settings.height, &height))
        return error;

    QColor background;
    if (args.contains(QStringLiteral("background")))
    {
        QString color;
        if (!readString(args, "background", &color, &error, true))
            return error;
        background = QColor(color);
        if (!background.isValid())
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("background 必须是 CSS 颜色"));
    }

    render2::SceneWorld world(project, tx.store->assets());
    anim::Cache cache;
    render2::Options options;
    options.projectFrame = frame;
    options.cameraUuid = cameraUuid;
    options.outputSize = QSize(width, height);
    options.background = background;
    options.videoFrame = tx.videoFrameProvider;
    options.htmlFrame = tx.htmlFrameProvider;

    const QImage image =
        render2::renderFrame(project, tx.store->assets(), options, world, cache);
    if (image.isNull())
        return failure(QStringLiteral("internal_error"),
                       QStringLiteral("预览渲染失败"));

    QString directory =
        QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (directory.isEmpty())
        directory = QDir::tempPath();
    if (!QDir().mkpath(directory))
        return failure(QStringLiteral("internal_error"),
                       QStringLiteral("无法创建预览临时目录"));

    const QString path = directory + QStringLiteral("/pvforge-preview-")
                         + model::newUuid() + QStringLiteral(".png");
    if (!image.save(path, "PNG"))
        return failure(QStringLiteral("internal_error"),
                       QStringLiteral("无法保存预览 PNG"));
    if (QFileInfo(path).size() > kMaxPreviewBytes)
    {
        QFile::remove(path);
        QJsonObject details;
        details.insert(QStringLiteral("maxBytes"), double(kMaxPreviewBytes));
        return failure(QStringLiteral("limit_exceeded"),
                       QStringLiteral("预览 PNG 超过 20MB"), details);
    }

    QJsonObject data;
    data.insert(QStringLiteral("path"), path);
    data.insert(QStringLiteral("width"), image.width());
    data.insert(QStringLiteral("height"), image.height());
    data.insert(QStringLiteral("frame"), frame);
    insertViewState(&data, tx.viewState);
    data.insert(QStringLiteral("cameraUuid"),
                uuidValue(cameraUuid));
    return success(data);
}

} // namespace pvf::mcp2::detail
