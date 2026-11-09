#include "mcp2/Internal.h"

#include <QDateTime>
#include <QDir>
#include <QJsonArray>
#include <QProcess>
#include <QRegularExpression>

#include <algorithm>
#include <climits>
#include <cmath>

namespace pvf::mcp2::detail {

namespace {

struct GitRun
{
    bool started = false;
    int exitCode = -1;
    QString out;
};

GitRun runGit(const QString& root, const QStringList& arguments)
{
    GitRun result;
    QProcess process;
    process.start(QStringLiteral("git"),
                  QStringList{QStringLiteral("-C"), root} + arguments);
    if (!process.waitForStarted(5000))
        return result;
    result.started = true;
    if (!process.waitForFinished(30000))
    {
        process.kill();
        process.waitForFinished(5000);
        return result;
    }
    result.exitCode =
        process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
    result.out = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    return result;
}

QJsonObject gitState(const QString& root)
{
    QJsonObject git;
    const GitRun version =
        runGit(root, {QStringLiteral("rev-parse"), QStringLiteral("--git-dir")});
    const bool available = version.started && version.exitCode == 0;
    git.insert(QStringLiteral("available"), available);

    if (!available)
    {
        git.insert(QStringLiteral("branch"), QJsonValue(QJsonValue::Null));
        git.insert(QStringLiteral("head"), QJsonValue(QJsonValue::Null));
        git.insert(QStringLiteral("clean"), false);
        return git;
    }

    const GitRun branch =
        runGit(root, {QStringLiteral("rev-parse"), QStringLiteral("--abbrev-ref"),
                      QStringLiteral("HEAD")});
    const GitRun head = runGit(root, {QStringLiteral("rev-parse"),
                                      QStringLiteral("--short"),
                                      QStringLiteral("HEAD")});
    const GitRun status = runGit(root, {QStringLiteral("status"),
                                        QStringLiteral("--porcelain")});
    git.insert(QStringLiteral("branch"),
               branch.exitCode == 0 ? QJsonValue(branch.out)
                                    : QJsonValue(QJsonValue::Null));
    git.insert(QStringLiteral("head"),
               head.exitCode == 0 ? QJsonValue(head.out)
                                  : QJsonValue(QJsonValue::Null));
    git.insert(QStringLiteral("clean"),
               status.exitCode == 0 && status.out.isEmpty());
    return git;
}

QJsonObject markerJson(const model::Marker& marker)
{
    return model::toJson(marker);
}

bool validColor(const QString& color)
{
    static const QRegularExpression pattern(
        QStringLiteral("^#(?:[0-9a-fA-F]{3,4}|[0-9a-fA-F]{6}|[0-9a-fA-F]{8})$"));
    return pattern.match(color).hasMatch();
}

QJsonObject commitJson(const store::GitCommit& commit)
{
    QJsonObject object;
    object.insert(QStringLiteral("hash"), commit.hash);
    object.insert(QStringLiteral("shortHash"), commit.hash.left(7));
    object.insert(QStringLiteral("message"), commit.message);
    object.insert(QStringLiteral("author"), QStringLiteral("PVForge AI"));
    object.insert(QStringLiteral("time"), commit.time.toUTC().toString(Qt::ISODate));
    object.insert(QStringLiteral("isUndo"),
                  commit.message.startsWith(QStringLiteral("pvf: undo ")));
    object.insert(QStringLiteral("isRedo"),
                  commit.message.startsWith(QStringLiteral("pvf: redo ")));
    object.insert(QStringLiteral("revertOf"), QJsonValue(QJsonValue::Null));
    return object;
}

} // namespace

Result projectGet(Tx& tx, const QJsonObject& args)
{
    Q_UNUSED(args);
    const model::Project& project = tx.p();

    QJsonObject paths;
    paths.insert(QStringLiteral("root"), tx.store->root());
    paths.insert(QStringLiteral("projectJson"),
                 tx.store->root() + QStringLiteral("/project.json"));
    paths.insert(QStringLiteral("entitiesDir"),
                 tx.store->root() + QStringLiteral("/entities"));
    paths.insert(QStringLiteral("assetsDir"),
                 tx.store->root() + QStringLiteral("/assets"));

    QJsonObject counts;
    counts.insert(QStringLiteral("entities"), project.entities.size());
    counts.insert(QStringLiteral("assets"), tx.store->assets().list().size());
    counts.insert(QStringLiteral("markers"), project.markers.size());

    QJsonObject write;
    write.insert(QStringLiteral("enabled"), true);
    write.insert(QStringLiteral("stopped"), false);

    QJsonObject data;
    data.insert(QStringLiteral("settings"), settingsJson(project.settings));
    data.insert(QStringLiteral("paths"), paths);
    data.insert(QStringLiteral("counts"), counts);
    data.insert(QStringLiteral("git"), gitState(tx.store->root()));
    data.insert(QStringLiteral("write"), write);
    return success(data);
}

Result projectSummary(Tx& tx, const QJsonObject& args)
{
    Q_UNUSED(args);
    const model::Project& project = tx.p();

    int durationFrames = 0;
    for (auto it = project.entities.constBegin(); it != project.entities.constEnd();
         ++it)
        durationFrames = std::max(durationFrames, it.value().end);

    QJsonObject data;
    data.insert(QStringLiteral("name"), project.settings.name);
    data.insert(QStringLiteral("width"), project.settings.width);
    data.insert(QStringLiteral("height"), project.settings.height);
    data.insert(QStringLiteral("fps"), project.settings.fps);
    data.insert(QStringLiteral("durationFrames"), durationFrames);
    data.insert(QStringLiteral("durationSeconds"),
                project.settings.fps > 0
                    ? durationFrames / double(project.settings.fps)
                    : 0.0);
    data.insert(QStringLiteral("background"), project.settings.background);
    data.insert(QStringLiteral("entityCount"), project.entities.size());
    data.insert(QStringLiteral("assetCount"), tx.store->assets().list().size());
    data.insert(QStringLiteral("markerCount"), project.markers.size());
    data.insert(QStringLiteral("activeCameraUuid"),
                tx.viewState.cameraUuid.isEmpty()
                    ? QJsonValue(QJsonValue::Null)
                    : QJsonValue(tx.viewState.cameraUuid));
    data.insert(QStringLiteral("writeEnabled"), true);
    return success(data);
}

Result projectUpdate(Tx& tx, const QJsonObject& args)
{
    static const QStringList allowed = {
        QStringLiteral("name"),    QStringLiteral("width"),
        QStringLiteral("height"),  QStringLiteral("fps"),
        QStringLiteral("background"), QStringLiteral("confirm"),
    };
    for (auto it = args.constBegin(); it != args.constEnd(); ++it)
    {
        if (!allowed.contains(it.key()))
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("未知参数 %1").arg(it.key()));
    }

    model::ProjectSettings& settings = tx.p().settings;
    QStringList changed;
    Result error;

    QString name;
    if (args.contains(QStringLiteral("name")) &&
        readString(args, "name", &name, &error, true))
    {
        if (name.isEmpty() || name.size() > 256)
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("name 必须是 1..256 字符"));
        if (name != settings.name)
        {
            settings.name = name;
            changed << QStringLiteral("name");
        }
    }
    else if (!error.message.isEmpty())
        return error;

    int width = settings.width;
    if (readInt(args, "width", &width, &error))
    {
        if (args.contains(QStringLiteral("width")))
        {
            if (width < 16 || width > 16384)
                return failure(QStringLiteral("invalid_argument"),
                               QStringLiteral("width 必须在 16..16384"));
            if (width != settings.width)
            {
                settings.width = width;
                changed << QStringLiteral("width");
            }
        }
    }
    else
        return error;

    int height = settings.height;
    if (readInt(args, "height", &height, &error))
    {
        if (args.contains(QStringLiteral("height")))
        {
            if (height < 16 || height > 16384)
                return failure(QStringLiteral("invalid_argument"),
                               QStringLiteral("height 必须在 16..16384"));
            if (height != settings.height)
            {
                settings.height = height;
                changed << QStringLiteral("height");
            }
        }
    }
    else
        return error;

    int fps = settings.fps;
    if (readInt(args, "fps", &fps, &error))
    {
        if (args.contains(QStringLiteral("fps")))
        {
            if (fps < 1 || fps > 240)
                return failure(QStringLiteral("invalid_argument"),
                               QStringLiteral("fps 必须在 1..240"));
            if (fps != settings.fps)
            {
                if (!tx.p().entities.isEmpty() && !confirmed(args))
                {
                    QJsonObject affected;
                    affected.insert(QStringLiteral("fps"), fps);
                    affected.insert(QStringLiteral("entityCount"),
                                    tx.p().entities.size());
                    QJsonObject details;
                    details.insert(QStringLiteral("requires"),
                                   QStringLiteral("confirm"));
                    details.insert(QStringLiteral("affected"), affected);
                    return failure(QStringLiteral("confirm_required"),
                                   QStringLiteral("非空工程修改 fps 需要 confirm"),
                                   details);
                }
                settings.fps = fps;
                changed << QStringLiteral("fps");
            }
        }
    }
    else
        return error;

    QString background;
    if (args.contains(QStringLiteral("background")) &&
        readString(args, "background", &background, &error, true))
    {
        if (!validColor(background))
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("background 必须是 CSS hex 颜色"));
        if (background != settings.background)
        {
            settings.background = background;
            changed << QStringLiteral("background");
        }
    }
    else if (!error.message.isEmpty())
        return error;

    if (!changed.isEmpty())
        tx.touchProject();

    QJsonObject data;
    data.insert(QStringLiteral("settings"), settingsJson(settings));
    QJsonArray changedArray;
    for (const QString& item : std::as_const(changed))
        changedArray.append(item);
    data.insert(QStringLiteral("changed"), changedArray);
    return tx.finish(data, QStringLiteral("pvf: update project \"%1\"")
                               .arg(settings.name));
}

Result markersList(Tx& tx, const QJsonObject& args)
{
    Result error;
    int fromFrame = 0;
    int toFrame = INT_MAX;
    int offset = 0;
    int limit = 100;
    if (!readInt(args, "fromFrame", &fromFrame, &error)
        || !readInt(args, "toFrame", &toFrame, &error)
        || !readInt(args, "offset", &offset, &error)
        || !readInt(args, "limit", &limit, &error))
        return error;
    if (fromFrame < 0 || offset < 0 || limit < 1 || limit > 500)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("分页或帧范围参数非法"));

    QVector<model::Marker> markers = tx.p().markers;
    std::stable_sort(markers.begin(), markers.end(),
                     [](const model::Marker& a, const model::Marker& b) {
                         return a.frame < b.frame;
                     });

    QJsonArray array;
    int total = 0;
    for (const model::Marker& marker : std::as_const(markers))
    {
        if (marker.frame < fromFrame || marker.frame > toFrame)
            continue;
        if (total >= offset && total < offset + limit)
            array.append(markerJson(marker));
        ++total;
    }

    QJsonObject data;
    data.insert(QStringLiteral("markers"), array);
    data.insert(QStringLiteral("page"), pageJson(offset, limit, total));
    return success(data);
}

Result markersSet(Tx& tx, const QJsonObject& args)
{
    static const QStringList allowed = {
        QStringLiteral("id"),    QStringLiteral("frame"),
        QStringLiteral("title"), QStringLiteral("note"),
        QStringLiteral("tags"),  QStringLiteral("color"),
    };
    for (auto it = args.constBegin(); it != args.constEnd(); ++it)
    {
        if (!allowed.contains(it.key()))
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("未知参数 %1").arg(it.key()));
    }

    Result error;
    QString id;
    if (!readString(args, "id", &id, &error))
        return error;

    int frame = 0;
    if (!readInt(args, "frame", &frame, &error, true))
        return error;
    if (frame < 0 || frame > 1000000)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("frame 必须在 0..1000000"));

    QString title;
    if (!readString(args, "title", &title, &error, true))
        return error;
    if (title.isEmpty() || title.size() > 256)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("title 必须是 1..256 字符"));

    QString note;
    if (!readString(args, "note", &note, &error))
        return error;
    if (note.size() > 4096)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("note 最长 4096 字符"));

    QStringList tags;
    if (args.contains(QStringLiteral("tags")))
    {
        const QJsonValue tagsValue = args.value(QStringLiteral("tags"));
        if (!tagsValue.isArray())
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("tags 必须是字符串数组"));
        const QJsonArray array = tagsValue.toArray();
        if (array.size() > 32)
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("tags 最多 32 个"));
        for (const QJsonValue& value : array)
        {
            if (!value.isString() || value.toString().size() > 64)
                return failure(QStringLiteral("invalid_argument"),
                               QStringLiteral("每个 tag 必须是最多 64 字符的字符串"));
            tags << value.toString();
        }
    }

    QString color = QStringLiteral("#ffbe3c");
    if (!readString(args, "color", &color, &error))
        return error;
    if (!validColor(color))
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("color 必须是 CSS hex 颜色"));

    bool created = false;
    int index = -1;
    for (int i = 0; i < tx.p().markers.size(); ++i)
    {
        if (!id.isEmpty() && tx.p().markers.at(i).id == id)
        {
            index = i;
            break;
        }
    }
    if (!id.isEmpty() && index < 0)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("标记不存在：%1").arg(id));

    model::Marker marker;
    if (index >= 0)
        marker = tx.p().markers.at(index);
    else
    {
        marker.id = model::newUuid();
        created = true;
    }
    marker.frame = frame;
    marker.title = title;
    marker.note = note;
    marker.tags = tags;
    marker.color = color;

    if (created)
        tx.p().markers.append(marker);
    else
        tx.p().markers[index] = marker;

    tx.touchProject();
    QJsonObject data;
    data.insert(QStringLiteral("marker"), markerJson(marker));
    data.insert(QStringLiteral("created"), created);
    return tx.finish(data, QStringLiteral("pvf: set marker \"%1\"").arg(title));
}

Result markersRemove(Tx& tx, const QJsonObject& args)
{
    Result error;
    QString id;
    if (!readString(args, "id", &id, &error, true))
        return error;
    if (id.isEmpty())
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("id 不能为空"));

    int index = -1;
    for (int i = 0; i < tx.p().markers.size(); ++i)
    {
        if (tx.p().markers.at(i).id == id)
        {
            index = i;
            break;
        }
    }
    if (index < 0)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("标记不存在：%1").arg(id));

    tx.p().markers.removeAt(index);
    tx.touchProject();
    QJsonObject data;
    data.insert(QStringLiteral("removed"), true);
    data.insert(QStringLiteral("id"), id);
    return tx.finish(data, QStringLiteral("pvf: remove marker"));
}

Result historyUndo(Tx& tx, const QJsonObject& args)
{
    Result error;
    int steps = 1;
    if (!readInt(args, "steps", &steps, &error))
        return error;
    if (steps < 1 || steps > 10)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("steps 必须在 1..10"));

    const QVector<store::GitCommit> before = tx.store->history(0);
    if (before.isEmpty())
        return failure(QStringLiteral("conflict"),
                       QStringLiteral("没有可撤销的提交"));

    QJsonArray reverted;
    const int count = std::min(steps, int(before.size()));
    for (int i = 0; i < count; ++i)
        reverted.append(commitJson(before.at(i)));

    for (int i = 0; i < steps; ++i)
    {
        QString message;
        if (!tx.store->undo(&message))
            return failure(QStringLiteral("conflict"), message);
    }

    const QVector<store::GitCommit> after = tx.store->history(0);
    QJsonObject newCommit;
    QString head;
    if (!after.isEmpty())
    {
        newCommit = commitJson(after.first());
        head = after.first().hash;
    }

    QJsonObject data;
    data.insert(QStringLiteral("reverted"), reverted);
    data.insert(QStringLiteral("newCommit"), newCommit);
    data.insert(QStringLiteral("head"), head);

    Result result = success(data);
    result.commitHash = head;
    return result;
}

Result historyRedo(Tx& tx, const QJsonObject& args)
{
    Result error;
    int steps = 1;
    if (!readInt(args, "steps", &steps, &error))
        return error;
    if (steps < 1 || steps > 10)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("steps 必须在 1..10"));

    QString previousHead;
    const QVector<store::GitCommit> before = tx.store->history(1);
    if (!before.isEmpty())
        previousHead = before.first().hash;

    for (int i = 0; i < steps; ++i)
    {
        QString message;
        if (!tx.store->redo(&message))
            return failure(QStringLiteral("conflict"), message);
    }

    const QVector<store::GitCommit> after = tx.store->history(0);
    QJsonArray redone;
    QJsonObject newCommit;
    QString head;
    for (const store::GitCommit& commit : after)
    {
        if (!previousHead.isEmpty() && commit.hash == previousHead)
            break;
        redone.append(commitJson(commit));
        if (head.isEmpty())
        {
            head = commit.hash;
            newCommit = commitJson(commit);
        }
    }

    QJsonObject data;
    data.insert(QStringLiteral("redone"), redone);
    data.insert(QStringLiteral("newCommit"), newCommit);
    data.insert(QStringLiteral("head"), head);

    Result result = success(data);
    result.commitHash = head;
    return result;
}

Result historyLog(Tx& tx, const QJsonObject& args)
{
    Result error;
    int offset = 0;
    int limit = 20;
    if (!readInt(args, "offset", &offset, &error)
        || !readInt(args, "limit", &limit, &error))
        return error;
    if (offset < 0 || limit < 1 || limit > 100)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("分页参数非法"));

    const QVector<store::GitCommit> all = tx.store->history(0);
    QJsonArray commits;
    const int total = all.size();
    for (int i = offset; i < total && i < offset + limit; ++i)
        commits.append(commitJson(all.at(i)));

    QJsonObject page = pageJson(offset, limit, total);
    QJsonObject data;
    data.insert(QStringLiteral("commits"), commits);
    data.insert(QStringLiteral("page"), page);
    return success(data);
}

} // namespace pvf::mcp2::detail
