#include "mcp/McpServer.h"

#include "core/Easing.h"
#include "core/Project.h"

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QHash>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <memory>

#ifndef PVFORGE_VERSION
#define PVFORGE_VERSION "0.2.0"
#endif

namespace pvf {

namespace {

constexpr int kMaxRequestLineBytes = 8 * 1024;
constexpr int kDefaultPageSize = 100;
constexpr int kMaxPageSize = 500;

QString protocolVersion()
{
    return QStringLiteral("2025-06-18");
}

QString entityTypeName(EntityType type)
{
    switch (type)
    {
    case EntityType::Image:
        return QStringLiteral("image");
    case EntityType::Video:
        return QStringLiteral("video");
    case EntityType::Text:
        return QStringLiteral("text");
    case EntityType::Vector:
        return QStringLiteral("vector");
    case EntityType::HtmlLayer:
        return QStringLiteral("htmlLayer");
    case EntityType::Camera:
        return QStringLiteral("camera");
    case EntityType::Audio:
        return QStringLiteral("audio");
    case EntityType::Folder:
        return QStringLiteral("folder");
    case EntityType::ComicText:
        return QStringLiteral("comicText");
    case EntityType::RadialLines:
        return QStringLiteral("radialLines");
    case EntityType::FlowLines:
        return QStringLiteral("flowLines");
    case EntityType::BrandWall:
        return QStringLiteral("brandWall");
    case EntityType::HalftoneDots:
        return QStringLiteral("halftoneDots");
    }
    return QStringLiteral("image");
}

bool isColorKey(const QString& key)
{
    const QString lower = key.toLower();
    return lower.endsWith(QLatin1String("color")) || lower.endsWith(QLatin1String("fill"))
           || lower.endsWith(QLatin1String("stroke"));
}

QJsonValue variantToJson(const QString& key, const QVariant& value)
{
    if (isColorKey(key))
    {
        const QColor color = value.value<QColor>();
        if (color.isValid())
            return QJsonValue(color.name(QColor::HexArgb));
    }
    return QJsonValue::fromVariant(value);
}

QJsonObject variantMapToJson(const QVariantMap& map)
{
    QJsonObject object;
    for (auto it = map.constBegin(); it != map.constEnd(); ++it)
        object.insert(it.key(), variantToJson(it.key(), it.value()));
    return object;
}

QJsonObject valuesToJson(const Entity& entity)
{
    QJsonObject object;
    for (auto it = entity.values.constBegin(); it != entity.values.constEnd(); ++it)
        object.insert(it.key(), it.value());
    return object;
}

QJsonArray trackToJson(const Track& track)
{
    QJsonArray keys;
    for (const Keyframe& key : track.keys)
    {
        QJsonObject object;
        object[QStringLiteral("frame")] = key.frame;
        object[QStringLiteral("value")] = key.value;
        object[QStringLiteral("ease")] = easingName(key.ease.easing);
        if (key.ease.easing == Easing::Bezier)
        {
            QJsonArray handles;
            handles.append(key.ease.inX);
            handles.append(key.ease.inY);
            handles.append(key.ease.outX);
            handles.append(key.ease.outY);
            object[QStringLiteral("bezier")] = handles;
        }
        keys.append(object);
    }
    return keys;
}

QJsonObject tracksToJson(const Entity& entity)
{
    QJsonObject object;
    for (auto it = entity.tracks.constBegin(); it != entity.tracks.constEnd(); ++it)
        object.insert(it.key(), trackToJson(it.value()));
    return object;
}

QJsonObject entityBriefJson(const Entity& entity)
{
    QJsonObject object;
    object[QStringLiteral("id")] = entity.id;
    object[QStringLiteral("type")] = entityTypeName(entity.type);
    object[QStringLiteral("name")] = entity.name;
    object[QStringLiteral("parent")] =
        entity.parent == kInvalidEntity ? QJsonValue(QJsonValue::Null)
                                        : QJsonValue(entity.parent);
    object[QStringLiteral("templateId")] = entity.templateId;
    object[QStringLiteral("hidden")] = entity.hidden;
    object[QStringLiteral("locked")] = entity.locked;
    object[QStringLiteral("startFrame")] =
        entity.data.value(QStringLiteral("startFrame"), 0.0).toDouble();
    object[QStringLiteral("values")] = valuesToJson(entity);
    return object;
}

QJsonObject entityFullJson(const Project& project, const Entity& entity)
{
    QJsonObject object = entityBriefJson(entity);
    object[QStringLiteral("sourcePath")] = project.storeAssetPath(entity.sourcePath);
    object[QStringLiteral("data")] = variantMapToJson(entity.data);
    object[QStringLiteral("tracks")] = tracksToJson(entity);
    return object;
}

QJsonObject errorPayload(const QString& code, const QString& message)
{
    QJsonObject error;
    error[QStringLiteral("code")] = code;
    error[QStringLiteral("message")] = message;
    QJsonObject payload;
    payload[QStringLiteral("ok")] = false;
    payload[QStringLiteral("error")] = error;
    return payload;
}

QJsonObject toolResult(const QJsonObject& structured, bool isError)
{
    const QByteArray text =
        QJsonDocument(structured).toJson(QJsonDocument::Compact);
    QJsonObject content;
    content[QStringLiteral("type")] = QStringLiteral("text");
    content[QStringLiteral("text")] = QString::fromUtf8(text);
    QJsonArray contentArray;
    contentArray.append(content);

    QJsonObject result;
    result[QStringLiteral("content")] = contentArray;
    result[QStringLiteral("structuredContent")] = structured;
    result[QStringLiteral("isError")] = isError;
    return result;
}

QJsonObject toolError(const QString& code, const QString& message)
{
    return toolResult(errorPayload(code, message), true);
}

QJsonObject toolSuccess(const QJsonObject& data, const QJsonObject& page = QJsonObject())
{
    QJsonObject structured;
    structured[QStringLiteral("ok")] = true;
    structured[QStringLiteral("data")] = data;
    if (!page.isEmpty())
        structured[QStringLiteral("page")] = page;
    return toolResult(structured, false);
}

QJsonObject pageJson(int offset, int limit, int total)
{
    const bool hasMore = offset + limit < total;
    QJsonObject page;
    page[QStringLiteral("offset")] = offset;
    page[QStringLiteral("limit")] = limit;
    page[QStringLiteral("total")] = total;
    page[QStringLiteral("hasMore")] = hasMore;
    page[QStringLiteral("nextOffset")] =
        hasMore ? QJsonValue(offset + limit) : QJsonValue(QJsonValue::Null);
    return page;
}

bool readIntArg(const QJsonObject& args, const QString& key, int fallback, int minimum,
                int maximum, int* out, QString* error)
{
    if (!args.contains(key) || args.value(key).isNull() || args.value(key).isUndefined())
    {
        *out = fallback;
        return true;
    }
    const QJsonValue value = args.value(key);
    if (!value.isDouble())
    {
        *error = QStringLiteral("%1 必须是整数").arg(key);
        return false;
    }
    const double number = value.toDouble();
    if (std::floor(number) != number || number < minimum || number > maximum)
    {
        *error = QStringLiteral("%1 必须在 %2..%3 之间").arg(key).arg(minimum).arg(maximum);
        return false;
    }
    *out = int(number);
    return true;
}

bool readPaging(const QJsonObject& args, int total, int* offset, int* limit,
                QJsonObject* page, QString* error)
{
    if (!readIntArg(args, QStringLiteral("offset"), 0, 0, INT_MAX - 1, offset, error))
        return false;
    if (!readIntArg(args, QStringLiteral("limit"), kDefaultPageSize, 1, kMaxPageSize,
                    limit, error))
        return false;
    *page = pageJson(*offset, *limit, total);
    return true;
}

QString authoringGuide()
{
    return QStringLiteral(
        "# PVForge 2D 创作指南\n"
        "\n"
        "## 坐标系与时间轴\n"
        "\n"
        "- 画布是 2D 像素坐标：原点在画面中心，x 向右，y 向下。\n"
        "- 时间轴首行是最前面的图层；新实体默认落在列表底部。\n"
        "- 变换由位置、旋转、轴心、缩放和父级链叠加而成；位置就是轴心的位置。\n"
        "- 摄像机是画布内的 2D 取景框，没有 z 轴，也没有 3D 物体。\n"
        "- 缓动属于“到达的关键帧”：1s 关键帧的缓动控制 0s → 1s 这一段。\n"
        "\n"
        "## 场景与注释\n"
        "\n"
        "- 场景不重叠、可以留空；AI 的写操作默认限制在当前场景。\n"
        "- 场景与注释数据层尚未合并到本构建，`scene.*` 和 `annotations.list` 当前"
        "返回 `not_available`，不要据此猜测数据。\n"
        "- 退休 = 透明度 0 + `_retired` 文件夹 + 锁定；永不删除。\n"
        "\n"
        "## 当前服务器\n"
        "\n"
        "- 本阶段 MCP 为只读：先读工程概况、时间轴、实体、素材和指南，再请人类修改。\n"
        "- 写工具（`entity.create` 等）一律返回 `permission_denied`。\n"
        "- 工具结果是结构化 JSON：成功为 `{ok,data,page?}`，失败为 "
        "`{ok:false,error:{code,message}}`。\n");
}

QString htmlContractGuide()
{
    return QStringLiteral(
        "# HTML 组件契约（M6）\n"
        "\n"
        "- 一层 = 一个页面 = 一个 HTML 资产 = 一次抓帧；宿主不拼接 snippet。\n"
        "- 变量使用 `组件名.变量名` 命名空间，内部一律 double；manifest 声明 "
        "name/type/min/max/default/integer。\n"
        "- 组件可以读取 `pvf.vars`、`pvf.frame`、`pvf.time`、`pvf.width`、"
        "`pvf.height` 和 `pvf.asset(name)`。\n"
        "- 组件完成后调用 `pvf.ready()` / `pvf.rendered()`；画面只能由变量、帧和时间决定。\n"
        "- 禁止网络、文件、媒体播放、自由 requestAnimationFrame、CSS animation/transition、"
        "WebGL、视频和音频。\n"
        "- 字体、图片、SVG 必须内联为 data URL 或引用工程内资产。\n"
        "- 本构建尚未启用 HTML 渲染与 `html.*` 写工具；此处只描述后续契约。\n");
}

QString workflowExamples()
{
    return QStringLiteral(
        "# 示例工作流\n"
        "\n"
        "## 检查一段动画\n"
        "\n"
        "1. `project.summary` 确认分辨率、帧率和总时长。\n"
        "2. `timeline.get` 分页读实体和关键帧，必要时用 `includeTracks=false` 只看结构。\n"
        "3. `entity.get` 读目标实体在指定帧的求值结果。\n"
        "4. `assets.list` 确认素材模板和引用次数。\n"
        "5. 需要改动时向人类描述方案；本阶段服务器只读。\n"
        "\n"
        "## 字幕条（后续 HTML）\n"
        "\n"
        "- manifest 暴露 `text`（字符串暂由 data 承载）与 `progress`（0..1）。\n"
        "- 每帧只依赖变量值；不使用 CSS 过渡。\n"
        "- 导出按输出分辨率抓帧，预览按显示尺寸抓帧。\n");
}

QString projectSchemaText()
{
    return QStringLiteral(
        "# PVForge project schema（当前 version 3）\n"
        "\n"
        "顶层字段：`format`、`version`、`name`、`author`、`description`、`width`、"
        "`height`、`fps`、`background`、`activeCamera`、`monitorView`、`entities[]`、"
        "`templates[]`。\n"
        "\n"
        "实体字段：`id`、`type`（image/video/text/vector/camera/audio/folder/"
        "comicText/radialLines/flowLines/brandWall/halftoneDots）、`parent`、"
        "`templateId`、`hidden`、`locked`、`name`、`sourcePath`、`data{}`、"
        "`values{}`、`tracks{属性: [{frame,value,ease,bezier?}]}`。\n"
        "\n"
        "坐标约定：2D 像素坐标，原点在画面中心，y 向下；帧号从 0 开始。\n"
        "`parent = -1` 表示根层。M6 计划新增 `scenes`、`annotations`、HTML 层和 "
        "`var.*` 轨道；本构建仍是 version 3。\n");
}

QJsonObject makeTool(const QString& name, const QString& description,
                     const QJsonObject& properties = QJsonObject(),
                     const QJsonArray& required = QJsonArray())
{
    QJsonObject schema;
    schema[QStringLiteral("type")] = QStringLiteral("object");
    if (!properties.isEmpty())
        schema[QStringLiteral("properties")] = properties;
    schema[QStringLiteral("additionalProperties")] = false;
    if (!required.isEmpty())
        schema[QStringLiteral("required")] = required;

    QJsonObject tool;
    tool[QStringLiteral("name")] = name;
    tool[QStringLiteral("description")] = description;
    tool[QStringLiteral("inputSchema")] = schema;
    return tool;
}

QJsonObject pageProperty(const QString& name, const QString& description, int fallback,
                         int minimum, int maximum)
{
    QJsonObject property;
    property[QStringLiteral("type")] = QStringLiteral("integer");
    property[QStringLiteral("description")] = description;
    property[QStringLiteral("minimum")] = minimum;
    property[QStringLiteral("maximum")] = maximum;
    property[QStringLiteral("default")] = fallback;
    return property;
}

QJsonArray toolDefinitions()
{
    QJsonArray tools;

    tools.append(makeTool(QStringLiteral("project.summary"),
                          QStringLiteral("工程概况：分辨率、帧率、时长、活动摄像机、"
                                         "实体数量和只读状态。")));

    QJsonObject timelineProperties;
    timelineProperties[QStringLiteral("offset")] =
        pageProperty(QStringLiteral("offset"), QStringLiteral("起始序号，从 0 开始"), 0,
                     0, INT_MAX - 1);
    timelineProperties[QStringLiteral("limit")] =
        pageProperty(QStringLiteral("limit"), QStringLiteral("每页数量，最多 500"),
                     kDefaultPageSize, 1, kMaxPageSize);
    QJsonObject includeTracks;
    includeTracks[QStringLiteral("type")] = QStringLiteral("boolean");
    includeTracks[QStringLiteral("description")] = QStringLiteral("是否包含轨道和关键帧");
    includeTracks[QStringLiteral("default")] = true;
    timelineProperties[QStringLiteral("includeTracks")] = includeTracks;
    tools.append(makeTool(QStringLiteral("timeline.get"),
                          QStringLiteral("按图层顺序分页读取实体、轨道和关键帧。"),
                          timelineProperties));

    QJsonObject entityProperties;
    entityProperties[QStringLiteral("id")] = QJsonObject{
        {QStringLiteral("type"), QStringLiteral("integer")},
        {QStringLiteral("description"), QStringLiteral("实体 id")}};
    QJsonObject frame;
    frame[QStringLiteral("type")] = QStringLiteral("number");
    frame[QStringLiteral("description")] = QStringLiteral("求值帧号，默认 0");
    frame[QStringLiteral("default")] = 0;
    entityProperties[QStringLiteral("frame")] = frame;
    tools.append(makeTool(QStringLiteral("entity.get"),
                          QStringLiteral("读取单个实体的完整状态，并返回指定帧的求值结果。"),
                          entityProperties,
                          QJsonArray{QStringLiteral("id")}));

    QJsonObject assetProperties;
    assetProperties[QStringLiteral("offset")] =
        pageProperty(QStringLiteral("offset"), QStringLiteral("起始序号，从 0 开始"), 0,
                     0, INT_MAX - 1);
    assetProperties[QStringLiteral("limit")] =
        pageProperty(QStringLiteral("limit"), QStringLiteral("每页数量，最多 500"),
                     kDefaultPageSize, 1, kMaxPageSize);
    tools.append(makeTool(QStringLiteral("assets.list"),
                          QStringLiteral("分页列出素材模板及其引用次数。"), assetProperties));

    QJsonObject guideProperties;
    QJsonObject topic;
    topic[QStringLiteral("type")] = QStringLiteral("string");
    topic[QStringLiteral("description")] =
        QStringLiteral("指南主题：authoring（默认）、html、examples");
    topic[QStringLiteral("enum")] = QJsonArray{QStringLiteral("authoring"),
                                               QStringLiteral("html"),
                                               QStringLiteral("examples")};
    guideProperties[QStringLiteral("topic")] = topic;
    tools.append(makeTool(QStringLiteral("guide.get"),
                          QStringLiteral("返回 2D 创作指南、HTML 契约或示例工作流。"),
                          guideProperties));

    tools.append(makeTool(
        QStringLiteral("scene.list"),
        QStringLiteral("场景列表；场景数据层尚未合并，当前返回 not_available。")));
    QJsonObject contextProperties;
    contextProperties[QStringLiteral("frame")] = frame;
    tools.append(makeTool(
        QStringLiteral("scene.context"),
        QStringLiteral("当前场景上下文；场景数据层尚未合并，当前返回 not_available。"),
        contextProperties));
    tools.append(makeTool(
        QStringLiteral("annotations.list"),
        QStringLiteral("全局标记与关键帧备注；注释数据层尚未合并，当前返回 "
                       "not_available。")));

    return tools;
}

QJsonArray resourceDefinitions()
{
    QJsonArray resources;
    QJsonObject guide;
    guide[QStringLiteral("uri")] = QStringLiteral("pvforge://guide/authoring");
    guide[QStringLiteral("name")] = QStringLiteral("PVForge 2D 创作指南");
    guide[QStringLiteral("description")] =
        QStringLiteral("坐标、图层顺序、变换、缓动、场景和只读边界。");
    guide[QStringLiteral("mimeType")] = QStringLiteral("text/markdown");
    resources.append(guide);

    QJsonObject schema;
    schema[QStringLiteral("uri")] = QStringLiteral("pvforge://project/schema");
    schema[QStringLiteral("name")] = QStringLiteral("PVForge 工程格式");
    schema[QStringLiteral("description")] =
        QStringLiteral("工程 JSON 的顶层字段、实体字段和坐标约定。");
    schema[QStringLiteral("mimeType")] = QStringLiteral("text/markdown");
    resources.append(schema);
    return resources;
}

QByteArray reasonPhrase(int status)
{
    switch (status)
    {
    case 200:
        return QByteArrayLiteral("OK");
    case 202:
        return QByteArrayLiteral("Accepted");
    case 400:
        return QByteArrayLiteral("Bad Request");
    case 401:
        return QByteArrayLiteral("Unauthorized");
    case 403:
        return QByteArrayLiteral("Forbidden");
    case 404:
        return QByteArrayLiteral("Not Found");
    case 405:
        return QByteArrayLiteral("Method Not Allowed");
    case 408:
        return QByteArrayLiteral("Request Timeout");
    case 411:
        return QByteArrayLiteral("Length Required");
    case 413:
        return QByteArrayLiteral("Content Too Large");
    case 415:
        return QByteArrayLiteral("Unsupported Media Type");
    case 431:
        return QByteArrayLiteral("Request Header Fields Too Large");
    case 503:
        return QByteArrayLiteral("Service Unavailable");
    default:
        return QByteArrayLiteral("Error");
    }
}

void sendHttp(QTcpSocket* socket, int status, const QByteArray& body,
              const QByteArray& contentType = QByteArrayLiteral("application/json"),
              const QByteArray& extraHeaders = QByteArray())
{
    QByteArray head = QByteArrayLiteral("HTTP/1.1 ") + QByteArray::number(status) + ' '
                      + reasonPhrase(status) + QByteArrayLiteral("\r\n");
    if (!body.isEmpty() && !contentType.isEmpty())
        head += QByteArrayLiteral("Content-Type: ") + contentType
                + QByteArrayLiteral("\r\n");
    head += QByteArrayLiteral("Content-Length: ") + QByteArray::number(body.size())
            + QByteArrayLiteral("\r\n");
    head += QByteArrayLiteral("Connection: close\r\n");
    head += extraHeaders;
    head += QByteArrayLiteral("\r\n");
    socket->write(head);
    if (!body.isEmpty())
        socket->write(body);
    socket->disconnectFromHost();
}

void sendRpcResult(QTcpSocket* socket, const QJsonValue& id, const QJsonObject& result)
{
    QJsonObject envelope;
    envelope[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    envelope[QStringLiteral("id")] = id;
    envelope[QStringLiteral("result")] = result;
    sendHttp(socket, 200,
             QJsonDocument(envelope).toJson(QJsonDocument::Compact));
}

void sendRpcError(QTcpSocket* socket, const QJsonValue& id, int code,
                  const QString& message)
{
    QJsonObject error;
    error[QStringLiteral("code")] = code;
    error[QStringLiteral("message")] = message;
    QJsonObject envelope;
    envelope[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    envelope[QStringLiteral("id")] = id;
    envelope[QStringLiteral("error")] = error;
    sendHttp(socket, 200,
             QJsonDocument(envelope).toJson(QJsonDocument::Compact));
}

void sendHttpError(QTcpSocket* socket, int status, const QString& code,
                   const QString& message)
{
    QJsonObject payload;
    payload[QStringLiteral("error")] = code;
    payload[QStringLiteral("message")] = message;
    const QByteArray body =
        QJsonDocument(payload).toJson(QJsonDocument::Compact);
    QByteArray extra;
    if (status == 401)
        extra = QByteArrayLiteral("WWW-Authenticate: Bearer realm=\"PVForge\"\r\n");
    else if (status == 405)
        extra = QByteArrayLiteral("Allow: POST\r\n");
    sendHttp(socket, status, body, QByteArrayLiteral("application/json"), extra);
}

bool constantTimeEquals(const QByteArray& a, const QByteArray& b)
{
    // Hash both sides so the comparison always runs over a fixed length and
    // never reveals the secret length through an early return.
    const QByteArray left =
        QCryptographicHash::hash(a, QCryptographicHash::Sha256);
    const QByteArray right =
        QCryptographicHash::hash(b, QCryptographicHash::Sha256);
    unsigned char diff = 0;
    for (int i = 0; i < left.size(); ++i)
        diff |= static_cast<unsigned char>(left.at(i) ^ right.at(i));
    return diff == 0;
}

bool isHeaderNameToken(const QByteArray& name)
{
    if (name.isEmpty())
        return false;
    for (const char raw : name)
    {
        const unsigned char c = static_cast<unsigned char>(raw);
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
            || (c >= '0' && c <= '9'))
            continue;
        switch (c)
        {
        case '!':
        case '#':
        case '$':
        case '%':
        case '&':
        case '\'':
        case '*':
        case '+':
        case '-':
        case '.':
        case '^':
        case '_':
        case '`':
        case '|':
        case '~':
            continue;
        default:
            return false;
        }
    }
    return true;
}

struct ParsedHead
{
    QByteArray method;
    QByteArray target;
    QHash<QByteArray, QByteArray> headers;
};

bool parseHttpHead(const QByteArray& head, ParsedHead* parsed, QString* error)
{
    if (head.contains('\0'))
    {
        *error = QStringLiteral("malformed request");
        return false;
    }

    QList<QByteArray> lines = head.split('\n');
    if (lines.isEmpty())
    {
        *error = QStringLiteral("malformed request");
        return false;
    }
    for (QByteArray& line : lines)
        if (line.endsWith('\r'))
            line.chop(1);
    while (!lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast();

    const QByteArray requestLine = lines.first();
    if (requestLine.size() > kMaxRequestLineBytes)
    {
        *error = QStringLiteral("request line is too long");
        return false;
    }
    const QList<QByteArray> request = requestLine.split(' ');
    if (request.size() != 3 || request.at(0).isEmpty() || request.at(1).isEmpty())
    {
        *error = QStringLiteral("malformed request line");
        return false;
    }
    if (request.at(2) != QByteArrayLiteral("HTTP/1.1"))
    {
        *error = QStringLiteral("only HTTP/1.1 is supported");
        return false;
    }
    parsed->method = request.at(0).toUpper();
    parsed->target = request.at(1);

    for (int i = 1; i < lines.size(); ++i)
    {
        const QByteArray& line = lines.at(i);
        if (line.isEmpty() || line.startsWith(' ') || line.startsWith('\t'))
        {
            *error = QStringLiteral("malformed header line");
            return false;
        }
        const int colon = line.indexOf(':');
        if (colon <= 0)
        {
            *error = QStringLiteral("malformed header line");
            return false;
        }
        const QByteArray name = line.left(colon);
        if (!isHeaderNameToken(name))
        {
            *error = QStringLiteral("malformed header name");
            return false;
        }
        const QByteArray value = line.mid(colon + 1).trimmed();
        if (value.contains('\r'))
        {
            *error = QStringLiteral("malformed header value");
            return false;
        }
        const QByteArray key = name.toLower();
        if (parsed->headers.contains(key))
        {
            // Duplicate Host/Content-Length/Authorization are a smuggling
            // and authentication-bypass vector; reject every duplicate.
            *error = QStringLiteral("duplicate request header");
            return false;
        }
        parsed->headers.insert(key, value);
    }
    return true;
}

struct PendingRequest
{
    QByteArray buffer;
    bool headerDone = false;
    bool done = false;
    QByteArray method;
    QByteArray target;
    QHash<QByteArray, QByteArray> headers;
    qint64 contentLength = 0;
};

} // namespace

McpServer::McpServer(const Project* project, QObject* parent)
    : QObject(parent)
    , m_project(project)
    , m_token(generateToken())
{
}

McpServer::~McpServer()
{
    stop();
}

void McpServer::setToken(const QByteArray& token)
{
    m_token = token.isEmpty() ? generateToken() : token;
}

void McpServer::setMaxBodyBytes(qint64 bytes)
{
    m_limits.maxBodyBytes = std::max<qint64>(1024, bytes);
}

void McpServer::setHttpLimits(const HttpLimits& limits)
{
    m_limits.maxBodyBytes = std::max<qint64>(1024, limits.maxBodyBytes);
    m_limits.maxHeaderBytes = std::max(1024, limits.maxHeaderBytes);
    m_limits.maxConnections = std::max(1, limits.maxConnections);
    m_limits.headerTimeoutMs = std::max(50, limits.headerTimeoutMs);
    m_limits.requestTimeoutMs = std::max(50, limits.requestTimeoutMs);
}

quint16 McpServer::start(quint16 port, QString* error)
{
    stop();

    // Fail closed: never expose an endpoint without a token.
    if (m_token.isEmpty())
        m_token = generateToken();

    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, &McpServer::acceptConnections);
    if (!m_server->listen(QHostAddress::LocalHost, port))
    {
        if (error)
            *error = m_server->errorString();
        m_server->deleteLater();
        m_server = nullptr;
        m_port = 0;
        return 0;
    }
    m_port = m_server->serverPort();
    return m_port;
}

void McpServer::stop()
{
    if (m_server)
    {
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
    }
    const QList<QTcpSocket*> sockets = findChildren<QTcpSocket*>();
    for (QTcpSocket* socket : sockets)
    {
        socket->abort();
        socket->deleteLater();
    }
    m_connections.clear();
    m_port = 0;
}

bool McpServer::isRunning() const
{
    return m_server && m_server->isListening();
}

QString McpServer::endpointUrl() const
{
    return QStringLiteral("http://127.0.0.1:%1/mcp").arg(m_port);
}

QStringList McpServer::readOnlyToolNames()
{
    return QStringList{QStringLiteral("project.summary"), QStringLiteral("timeline.get"),
                       QStringLiteral("entity.get"), QStringLiteral("assets.list"),
                       QStringLiteral("guide.get"), QStringLiteral("scene.list"),
                       QStringLiteral("scene.context"), QStringLiteral("annotations.list")};
}

QByteArray McpServer::generateToken()
{
    QByteArray token(32, '\0');
    QRandomGenerator* generator = QRandomGenerator::system();
    for (int offset = 0; offset < token.size(); offset += 8)
    {
        const quint64 value = generator->generate64();
        std::memcpy(token.data() + offset, &value,
                    size_t(std::min<qsizetype>(8, token.size() - offset)));
    }
    return token.toHex();
}

bool McpServer::isAllowedHost(const QByteArray& host) const
{
    const QByteArray value = host.trimmed();
    if (value.isEmpty() || value.contains(' ') || value.contains('\t')
        || value.contains(',') || value.contains('/'))
        return false;

    const int colon = value.lastIndexOf(':');
    if (colon <= 0 || colon == value.size() - 1)
        return false;

    const QByteArray name = value.left(colon).toLower();
    const QByteArray portText = value.mid(colon + 1);
    bool digits = true;
    for (const char c : portText)
        digits = digits && c >= '0' && c <= '9';
    if (!digits)
        return false;

    bool portOk = false;
    const int port = portText.toInt(&portOk);
    if (!portOk || port != int(m_port))
        return false;

    return name == QByteArrayLiteral("127.0.0.1")
           || name == QByteArrayLiteral("localhost");
}

bool McpServer::isAllowedOrigin(const QByteArray& origin) const
{
    const QByteArray value = origin.trimmed();
    if (value.isEmpty())
        return false;
    const QByteArray lower = value.toLower();
    // Opaque origins (sandboxed pages, file://) are never allowed.
    if (lower == QByteArrayLiteral("null"))
        return false;

    const QByteArray prefix = QByteArrayLiteral("http://");
    if (!lower.startsWith(prefix))
        return false;

    // Reuse the Host rules: only the exact loopback authority is accepted,
    // so a trailing slash, userinfo, another scheme or another port fails.
    return isAllowedHost(lower.mid(prefix.size()));
}

void McpServer::acceptConnections()
{
    if (!m_server)
        return;
    while (QTcpSocket* socket = m_server->nextPendingConnection())
    {
        socket->setParent(this);
        connect(socket, &QTcpSocket::disconnected, this,
                [this, socket] { m_connections.remove(socket); });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);

        if (m_connections.size() >= m_limits.maxConnections)
        {
            sendHttpError(socket, 503, QStringLiteral("server_busy"),
                          QStringLiteral("Too many concurrent connections"));
            QTimer::singleShot(2000, socket, [socket] {
                socket->abort();
                socket->deleteLater();
            });
            continue;
        }

        m_connections.insert(socket);
        attachSocket(socket);
    }
}

void McpServer::attachSocket(QTcpSocket* socket)
{
    socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);

    if (!socket->peerAddress().isLoopback())
    {
        sendHttpError(socket, 403, QStringLiteral("loopback_only"),
                      QStringLiteral("MCP only accepts loopback clients"));
        return;
    }

    auto state = std::make_shared<PendingRequest>();
    auto* timer = new QTimer(socket);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this, socket, state] {
        if (state->done)
            return;
        state->done = true;
        sendHttpError(socket, 408, QStringLiteral("request_timeout"),
                      QStringLiteral("Request timed out"));
    });
    timer->start(m_limits.headerTimeoutMs);

    connect(socket, &QTcpSocket::readyRead, this, [this, socket, state, timer] {
        if (state->done)
        {
            socket->readAll();
            return;
        }
        state->buffer.append(socket->readAll());

        if (!state->headerDone)
        {
            const int headerEnd = state->buffer.indexOf("\r\n\r\n");
            if (headerEnd < 0)
            {
                if (state->buffer.size() > m_limits.maxHeaderBytes)
                {
                    state->done = true;
                    sendHttpError(socket, 431, QStringLiteral("header_too_large"),
                                  QStringLiteral("Request headers exceed the limit"));
                }
                return;
            }
            if (headerEnd > m_limits.maxHeaderBytes)
            {
                state->done = true;
                sendHttpError(socket, 431, QStringLiteral("header_too_large"),
                              QStringLiteral("Request headers exceed the limit"));
                return;
            }

            const QByteArray head = state->buffer.left(headerEnd);
            state->buffer.remove(0, headerEnd + 4);
            state->headerDone = true;
            timer->start(m_limits.requestTimeoutMs);

            ParsedHead parsed;
            QString error;
            if (!parseHttpHead(head, &parsed, &error))
            {
                state->done = true;
                sendHttpError(socket, 400, QStringLiteral("bad_request"), error);
                return;
            }
            state->method = parsed.method;
            state->target = parsed.target;
            state->headers = parsed.headers;

            // Host is checked before anything else so a DNS-rebinding
            // request can never reach path, auth or body handling.
            if (!isAllowedHost(state->headers.value(QByteArrayLiteral("host"))))
            {
                state->done = true;
                sendHttpError(socket, 403, QStringLiteral("bad_host"),
                              QStringLiteral("Host must be 127.0.0.1 or localhost "
                                             "with the server port"));
                return;
            }

            if (state->headers.contains(QByteArrayLiteral("origin"))
                && !isAllowedOrigin(
                    state->headers.value(QByteArrayLiteral("origin"))))
            {
                state->done = true;
                sendHttpError(socket, 403, QStringLiteral("bad_origin"),
                              QStringLiteral("Origin is not allowed"));
                return;
            }

            const QByteArray fetchSite =
                state->headers.value(QByteArrayLiteral("sec-fetch-site")).toLower();
            if (!fetchSite.isEmpty() && fetchSite != "same-origin"
                && fetchSite != "same-site" && fetchSite != "none")
            {
                state->done = true;
                sendHttpError(socket, 403, QStringLiteral("bad_origin"),
                              QStringLiteral("Cross-site requests are not allowed"));
                return;
            }

            if (state->method != "POST")
            {
                state->done = true;
                sendHttpError(socket, 405, QStringLiteral("method_not_allowed"),
                              QStringLiteral("Use POST /mcp"));
                return;
            }

            QByteArray path = state->target;
            const int query = path.indexOf('?');
            if (query >= 0)
                path.truncate(query);
            if (path != "/mcp" && path != "/mcp/")
            {
                state->done = true;
                sendHttpError(socket, 404, QStringLiteral("not_found"),
                              QStringLiteral("MCP endpoint is /mcp"));
                return;
            }

            const QByteArray authorization =
                state->headers.value(QByteArrayLiteral("authorization"));
            const QByteArray scheme = QByteArrayLiteral("bearer ");
            if (!authorization.toLower().startsWith(scheme)
                || !constantTimeEquals(authorization.mid(scheme.size()).trimmed(),
                                       m_token))
            {
                state->done = true;
                sendHttpError(socket, 401, QStringLiteral("unauthorized"),
                              QStringLiteral("Missing or invalid Bearer token"));
                return;
            }

            const QByteArray contentType =
                state->headers.value(QByteArrayLiteral("content-type"));
            const QByteArray mediaType =
                contentType.split(';').first().trimmed().toLower();
            if (mediaType != "application/json")
            {
                state->done = true;
                sendHttpError(socket, 415, QStringLiteral("unsupported_media_type"),
                              QStringLiteral("Content-Type must be application/json"));
                return;
            }

            // Never accept chunked framing: it would bypass Content-Length
            // accounting and enables CL.TE request smuggling.
            if (state->headers.contains(QByteArrayLiteral("transfer-encoding")))
            {
                state->done = true;
                sendHttpError(socket, 400, QStringLiteral("bad_request"),
                              QStringLiteral("Transfer-Encoding is not supported"));
                return;
            }

            const QByteArray contentLength =
                state->headers.value(QByteArrayLiteral("content-length"));
            if (contentLength.isEmpty())
            {
                state->done = true;
                sendHttpError(socket, 411, QStringLiteral("length_required"),
                              QStringLiteral("Content-Length is required"));
                return;
            }

            bool lengthOk = true;
            for (const char c : contentLength)
                lengthOk = lengthOk && c >= '0' && c <= '9';
            const qint64 length =
                lengthOk ? contentLength.toLongLong(&lengthOk) : 0;
            if (!lengthOk)
            {
                state->done = true;
                sendHttpError(socket, 400, QStringLiteral("bad_request"),
                              QStringLiteral("Invalid Content-Length"));
                return;
            }
            if (length > m_limits.maxBodyBytes || length > INT_MAX)
            {
                state->done = true;
                sendHttpError(socket, 413, QStringLiteral("payload_too_large"),
                              QStringLiteral("Request body exceeds %1 bytes")
                                  .arg(m_limits.maxBodyBytes));
                return;
            }
            state->contentLength = length;
        }

        if (state->buffer.size() < state->contentLength)
            return;

        state->done = true;
        const QByteArray body = state->buffer.left(int(state->contentLength));
        handleRequest(socket, state->method, state->target, QByteArray(), body);
    });
}

void McpServer::handleRequest(QTcpSocket* socket, const QByteArray& method,
                              const QByteArray& target, const QByteArray& headers,
                              const QByteArray& body)
{
    Q_UNUSED(method)
    Q_UNUSED(target)
    Q_UNUSED(headers)

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError)
    {
        sendRpcError(socket, QJsonValue::Null, -32700, QStringLiteral("Parse error"));
        return;
    }
    if (!document.isObject())
    {
        sendRpcError(socket, QJsonValue::Null, -32600,
                     QStringLiteral("Invalid Request: expected a single JSON-RPC object"));
        return;
    }
    handleRpc(socket, document.object());
}

void McpServer::handleRpc(QTcpSocket* socket, const QJsonObject& request)
{
    const bool notification = !request.contains(QStringLiteral("id"));
    const QJsonValue id = request.value(QStringLiteral("id"));
    const QString version = request.value(QStringLiteral("jsonrpc")).toString();
    const QString method = request.value(QStringLiteral("method")).toString();

    if (version != QLatin1String("2.0") || method.isEmpty())
    {
        if (notification)
        {
            sendHttp(socket, 202, QByteArray());
            return;
        }
        sendRpcError(socket, id, -32600, QStringLiteral("Invalid Request"));
        return;
    }

    if (notification)
    {
        // Notifications, including notifications/cancelled and
        // notifications/initialized, have no response body.
        sendHttp(socket, 202, QByteArray());
        return;
    }

    if (method == QLatin1String("initialize"))
    {
        const QJsonObject params = request.value(QStringLiteral("params")).toObject();
        QString negotiated = params.value(QStringLiteral("protocolVersion")).toString();
        if (negotiated.isEmpty())
            negotiated = protocolVersion();

        QJsonObject capabilities;
        capabilities[QStringLiteral("tools")] =
            QJsonObject{{QStringLiteral("listChanged"), false}};
        capabilities[QStringLiteral("resources")] =
            QJsonObject{{QStringLiteral("subscribe"), false},
                        {QStringLiteral("listChanged"), false}};

        QJsonObject serverInfo;
        serverInfo[QStringLiteral("name")] = QStringLiteral("PVForge");
        serverInfo[QStringLiteral("version")] = QStringLiteral(PVFORGE_VERSION);

        QJsonObject result;
        result[QStringLiteral("protocolVersion")] = negotiated;
        result[QStringLiteral("capabilities")] = capabilities;
        result[QStringLiteral("serverInfo")] = serverInfo;
        result[QStringLiteral("instructions")] =
            QStringLiteral("PVForge MCP 只读阶段：可以读取工程、时间轴、实体、素材和指南；"
                           "写工具一律返回 permission_denied。");
        sendRpcResult(socket, id, result);
        return;
    }

    if (method == QLatin1String("ping"))
    {
        sendRpcResult(socket, id, QJsonObject());
        return;
    }

    if (method == QLatin1String("tools/list"))
    {
        QJsonObject result;
        result[QStringLiteral("tools")] = toolDefinitions();
        sendRpcResult(socket, id, result);
        return;
    }

    if (method == QLatin1String("tools/call"))
    {
        const QJsonObject params = request.value(QStringLiteral("params")).toObject();
        const QString name = params.value(QStringLiteral("name")).toString();
        if (name.isEmpty())
        {
            sendRpcError(socket, id, -32602, QStringLiteral("Missing tool name"));
            return;
        }

        const QJsonValue argsValue = params.value(QStringLiteral("arguments"));
        if (!argsValue.isUndefined() && !argsValue.isNull() && !argsValue.isObject())
        {
            const QJsonObject result =
                toolError(QStringLiteral("invalid_argument"),
                          QStringLiteral("arguments 必须是 JSON 对象"));
            emit toolInvoked(name, QStringLiteral("{}"),
                             QString::fromUtf8(
                                 QJsonDocument(result).toJson(QJsonDocument::Compact)),
                             0, false);
            sendRpcResult(socket, id, result);
            return;
        }

        const QJsonObject args = argsValue.toObject();
        QElapsedTimer timer;
        timer.start();
        bool ok = false;
        const QJsonObject result = executeTool(name, args, &ok);
        const qint64 duration = timer.elapsed();

        emit toolInvoked(
            name,
            QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact)),
            QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact)),
            duration, ok);
        sendRpcResult(socket, id, result);
        return;
    }

    if (method == QLatin1String("resources/list"))
    {
        QJsonObject result;
        result[QStringLiteral("resources")] = resourceDefinitions();
        sendRpcResult(socket, id, result);
        return;
    }

    if (method == QLatin1String("resources/read"))
    {
        const QJsonObject params = request.value(QStringLiteral("params")).toObject();
        const QString uri = params.value(QStringLiteral("uri")).toString();
        if (uri.isEmpty())
        {
            sendRpcError(socket, id, -32602, QStringLiteral("Missing resource uri"));
            return;
        }

        QString text;
        if (uri == QLatin1String("pvforge://guide/authoring"))
            text = authoringGuide();
        else if (uri == QLatin1String("pvforge://project/schema"))
            text = projectSchemaText();
        else
        {
            QJsonObject error;
            error[QStringLiteral("code")] = -32002;
            error[QStringLiteral("message")] =
                QStringLiteral("Resource not found: %1").arg(uri);
            QJsonObject envelope;
            envelope[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
            envelope[QStringLiteral("id")] = id;
            envelope[QStringLiteral("error")] = error;
            sendHttp(socket, 200,
                     QJsonDocument(envelope).toJson(QJsonDocument::Compact));
            return;
        }

        QJsonObject content;
        content[QStringLiteral("uri")] = uri;
        content[QStringLiteral("mimeType")] = QStringLiteral("text/markdown");
        content[QStringLiteral("text")] = text;
        QJsonObject result;
        result[QStringLiteral("contents")] = QJsonArray{content};
        sendRpcResult(socket, id, result);
        return;
    }

    sendRpcError(socket, id, -32601, QStringLiteral("Method not found"));
}

QJsonObject McpServer::executeTool(const QString& name, const QJsonObject& args,
                                   bool* ok)
{
    *ok = false;

    static const QStringList writeTools{
        QStringLiteral("entity.create"),       QStringLiteral("entity.update"),
        QStringLiteral("entity.move"),         QStringLiteral("folder.create"),
        QStringLiteral("keyframes.set"),       QStringLiteral("easing.set"),
        QStringLiteral("entities.retire"),     QStringLiteral("html.create_layer"),
        QStringLiteral("html.set_variables"),  QStringLiteral("variables.set_keyframes"),
        QStringLiteral("assets.import_blob"),  QStringLiteral("scene.create"),
        QStringLiteral("batch.run")};
    if (writeTools.contains(name))
        return toolError(QStringLiteral("permission_denied"),
                         QStringLiteral("MCP 当前为只读模式，写工具未启用"));

    if (!readOnlyToolNames().contains(name))
        return toolError(QStringLiteral("not_available"),
                         QStringLiteral("未知工具：%1").arg(name));

    if (name == QLatin1String("scene.list")
        || name == QLatin1String("scene.context"))
        return toolError(QStringLiteral("not_available"),
                         QStringLiteral("场景数据层尚未合并；该工具将在场景功能落地后启用"));

    if (name == QLatin1String("annotations.list"))
        return toolError(QStringLiteral("not_available"),
                         QStringLiteral("注释数据层尚未合并；该工具将在注释功能落地后启用"));

    if (name == QLatin1String("project.summary"))
    {
        QJsonObject data;
        data[QStringLiteral("name")] = m_project->name;
        data[QStringLiteral("filePath")] = m_project->filePath;
        data[QStringLiteral("width")] = m_project->width;
        data[QStringLiteral("height")] = m_project->height;
        data[QStringLiteral("fps")] = m_project->fps;
        data[QStringLiteral("durationFrames")] = m_project->timelineLength();
        data[QStringLiteral("durationSeconds")] =
            m_project->fps > 0
                ? double(m_project->timelineLength()) / double(m_project->fps)
                : 0.0;
        data[QStringLiteral("frameRange")] =
            QJsonObject{{QStringLiteral("first"), 0},
                        {QStringLiteral("last"), m_project->timelineLength()}};
        data[QStringLiteral("background")] =
            m_project->background.name(QColor::HexArgb);
        data[QStringLiteral("modified")] = m_project->modified;
        data[QStringLiteral("entityCount")] = m_project->entities().size();
        data[QStringLiteral("templateCount")] = m_project->templateCount();
        data[QStringLiteral("readOnly")] = true;
        data[QStringLiteral("scenesAvailable")] = false;
        data[QStringLiteral("annotationsAvailable")] = false;
        data[QStringLiteral("scenes")] = QJsonArray();

        const Entity* camera = m_project->entity(m_project->activeCamera);
        if (camera)
        {
            data[QStringLiteral("activeCamera")] =
                QJsonObject{{QStringLiteral("id"), camera->id},
                            {QStringLiteral("name"), camera->name}};
        }
        else
        {
            data[QStringLiteral("activeCamera")] = QJsonValue(QJsonValue::Null);
        }

        *ok = true;
        return toolSuccess(data);
    }

    if (name == QLatin1String("timeline.get"))
    {
        int offset = 0;
        int limit = kDefaultPageSize;
        QJsonObject page;
        QString error;
        const QVector<int> order = m_project->entityOrder();
        if (!readPaging(args, order.size(), &offset, &limit, &page, &error))
            return toolError(QStringLiteral("invalid_argument"), error);

        const bool includeTracks =
            args.value(QStringLiteral("includeTracks")).toBool(true);

        QJsonArray entities;
        const int end = std::min<int>(order.size(), offset + limit);
        for (int i = offset; i < end; ++i)
        {
            const Entity& entity = m_project->entities().at(order.at(i));
            QJsonObject object = entityBriefJson(entity);
            if (includeTracks)
                object[QStringLiteral("tracks")] = tracksToJson(entity);
            entities.append(object);
        }

        QJsonObject data;
        data[QStringLiteral("entities")] = entities;
        *ok = true;
        return toolSuccess(data, page);
    }

    if (name == QLatin1String("entity.get"))
    {
        const QJsonValue idValue = args.value(QStringLiteral("id"));
        if (!idValue.isDouble() || std::floor(idValue.toDouble()) != idValue.toDouble())
            return toolError(QStringLiteral("invalid_argument"),
                             QStringLiteral("id 必须是整数"));
        const int id = int(idValue.toDouble());
        const Entity* entity = m_project->entity(id);
        if (!entity)
            return toolError(QStringLiteral("invalid_argument"),
                             QStringLiteral("实体不存在：id=%1").arg(id));

        const QJsonValue frameValue = args.value(QStringLiteral("frame"));
        double frame = 0.0;
        if (!frameValue.isUndefined() && !frameValue.isNull())
        {
            if (!frameValue.isDouble())
                return toolError(QStringLiteral("invalid_argument"),
                                 QStringLiteral("frame 必须是数字"));
            frame = frameValue.toDouble();
        }

        QJsonObject data = entityFullJson(*m_project, *entity);
        data[QStringLiteral("frame")] = frame;

        QStringList properties{QStringLiteral("x"), QStringLiteral("y"),
                               QStringLiteral("rotation"), QStringLiteral("scaleX"),
                               QStringLiteral("scaleY"), QStringLiteral("opacity")};
        for (auto it = entity->values.constBegin(); it != entity->values.constEnd(); ++it)
            if (!properties.contains(it.key()))
                properties.append(it.key());
        for (auto it = entity->tracks.constBegin(); it != entity->tracks.constEnd(); ++it)
            if (!properties.contains(it.key()))
                properties.append(it.key());

        QJsonObject evaluated;
        for (const QString& property : properties)
            evaluated[property] = m_project->value(id, property, frame);
        data[QStringLiteral("evaluated")] = evaluated;

        *ok = true;
        return toolSuccess(data);
    }

    if (name == QLatin1String("assets.list"))
    {
        int offset = 0;
        int limit = kDefaultPageSize;
        QJsonObject page;
        QString error;
        if (!readPaging(args, m_project->templateCount(), &offset, &limit, &page,
                        &error))
            return toolError(QStringLiteral("invalid_argument"), error);

        QJsonArray assets;
        const int end = std::min(m_project->templateCount(), offset + limit);
        for (int i = offset; i < end; ++i)
        {
            const Template& tmpl = m_project->templates().at(i);
            int usedBy = 0;
            for (const Entity& entity : m_project->entities())
                if (entity.templateId == tmpl.id)
                    ++usedBy;

            QJsonObject object;
            object[QStringLiteral("id")] = tmpl.id;
            object[QStringLiteral("type")] = entityTypeName(tmpl.type);
            object[QStringLiteral("name")] = tmpl.name;
            object[QStringLiteral("path")] = m_project->storeAssetPath(tmpl.path);
            object[QStringLiteral("duration")] = tmpl.duration;
            object[QStringLiteral("usedBy")] = usedBy;
            assets.append(object);
        }

        QJsonObject data;
        data[QStringLiteral("assets")] = assets;
        *ok = true;
        return toolSuccess(data, page);
    }

    if (name == QLatin1String("guide.get"))
    {
        const QString topic =
            args.value(QStringLiteral("topic")).toString(QStringLiteral("authoring"));
        QString text;
        if (topic == QLatin1String("authoring"))
            text = authoringGuide();
        else if (topic == QLatin1String("html"))
            text = htmlContractGuide();
        else if (topic == QLatin1String("examples"))
            text = workflowExamples();
        else
            return toolError(QStringLiteral("invalid_argument"),
                             QStringLiteral("未知主题：%1（可用：authoring、html、"
                                            "examples）")
                                 .arg(topic));

        QJsonObject data;
        data[QStringLiteral("topic")] = topic;
        data[QStringLiteral("markdown")] = text;
        data[QStringLiteral("resources")] =
            QJsonArray{QStringLiteral("pvforge://guide/authoring"),
                       QStringLiteral("pvforge://project/schema")};
        *ok = true;
        return toolSuccess(data);
    }

    return toolError(QStringLiteral("not_available"),
                     QStringLiteral("工具未实现：%1").arg(name));
}

} // namespace pvf
