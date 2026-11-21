#include "mcp2/Internal.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

#include <initializer_list>

namespace pvf::mcp2::detail {

namespace {

QString easingWhitelist()
{
    static const QString list = acceptedEasingNames().join(QStringLiteral(", "));
    return list;
}

QJsonObject prop(const QString& type, const QString& description)
{
    QJsonObject object;
    object.insert(QStringLiteral("type"), type);
    if (!description.isEmpty())
        object.insert(QStringLiteral("description"), description);
    return object;
}

QJsonObject arrayOf(const QString& itemType, const QString& description)
{
    QJsonObject items;
    items.insert(QStringLiteral("type"), itemType);
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("array"));
    object.insert(QStringLiteral("items"), items);
    if (!description.isEmpty())
        object.insert(QStringLiteral("description"), description);
    return object;
}

QJsonObject objectSchema(std::initializer_list<QPair<QString, QJsonObject>> fields,
                         const QStringList& required = {})
{
    QJsonObject properties;
    for (const auto& field : fields)
        properties.insert(field.first, field.second);

    QJsonObject schema;
    schema.insert(QStringLiteral("type"), QStringLiteral("object"));
    schema.insert(QStringLiteral("properties"), properties);
    if (!required.isEmpty())
    {
        QJsonArray requiredArray;
        for (const QString& name : required)
            requiredArray.append(name);
        schema.insert(QStringLiteral("required"), requiredArray);
    }
    schema.insert(QStringLiteral("additionalProperties"), false);
    return schema;
}

QJsonObject openSchema(std::initializer_list<QPair<QString, QJsonObject>> fields,
                       const QStringList& required = {})
{
    QJsonObject schema = objectSchema(fields, required);
    schema.remove(QStringLiteral("additionalProperties"));
    return schema;
}

QJsonObject toolSchema(const QString& name, const QString& description,
                       const QJsonObject& inputSchema)
{
    QJsonObject object;
    object.insert(QStringLiteral("name"), name);
    object.insert(QStringLiteral("description"), description);
    object.insert(QStringLiteral("inputSchema"), inputSchema);
    return object;
}

// Single source of truth for the per-kind animation parameter contract.  It is
// repeated in tools/list descriptions and schema.get so an agent that reads
// only one of them still learns the complete required set instead of failing
// one missing field at a time.
QString animationParamsSummary()
{
    return QStringLiteral(
        "各 content.kind 必填的动画参数：text → pixelSize/lineHeight/"
        "letterSpacing/padding/boxWidth/boxHeight/strokeWidth；"
        "vector → width/height/cornerRadius/strokeWidth；"
        "audio（asset.type=audio）→ volume；"
        "image/video/html/camera/empty → 无（content 可省略或传 {}；"
        "HTML 数字变量写进 htmlVars）");
}

// Content is a tagged union: `kind` plus a same-named sub-object (text/vector/
// camera) or flat asset fields.  It only holds definition parameters; the
// animation parameters live in each whole-frame keyframe's `content` object.
QJsonObject contentSchema()
{
    const QJsonObject text = objectSchema({
        {QStringLiteral("text"), prop(QStringLiteral("string"), QStringLiteral("文字，可含换行"))},
        {QStringLiteral("fontFamily"), prop(QStringLiteral("string"), QStringLiteral("空 = 默认字体"))},
        {QStringLiteral("pixelSize"), prop(QStringLiteral("integer"), QStringLiteral("1..8192，默认 96；静态基准值，动画值写在 keyframes[].content"))},
        {QStringLiteral("color"), prop(QStringLiteral("string"), QStringLiteral("CSS hex 文字色"))},
        {QStringLiteral("strokeColor"), prop(QStringLiteral("string"), QStringLiteral("CSS hex 文字描边色"))},
        {QStringLiteral("strokeWidth"), prop(QStringLiteral("number"), QStringLiteral("0..1000；静态基准值"))},
        {QStringLiteral("align"), prop(QStringLiteral("integer"), QStringLiteral("0..65535（Qt 对齐标志）"))},
        {QStringLiteral("lineHeight"), prop(QStringLiteral("number"), QStringLiteral("0.1..10；静态基准值"))},
        {QStringLiteral("letterSpacing"), prop(QStringLiteral("number"), QStringLiteral("静态基准值"))},
        {QStringLiteral("padding"), prop(QStringLiteral("number"), QStringLiteral("0..100000；静态基准值"))},
        {QStringLiteral("boxWidth"), prop(QStringLiteral("number"), QStringLiteral("静态基准值"))},
        {QStringLiteral("boxHeight"), prop(QStringLiteral("number"), QStringLiteral("静态基准值"))},
        {QStringLiteral("bold"), prop(QStringLiteral("boolean"), QString())},
        {QStringLiteral("italic"), prop(QStringLiteral("boolean"), QString())},
        {QStringLiteral("underline"), prop(QStringLiteral("boolean"), QString())},
        {QStringLiteral("vertical"), prop(QStringLiteral("boolean"), QString())},
        {QStringLiteral("verticalRtl"), prop(QStringLiteral("boolean"), QString())},
    });

    const QJsonObject vector = objectSchema({
        {QStringLiteral("shape"), prop(QStringLiteral("integer"), QStringLiteral("整数枚举：0 rect、1 ellipse、2 line、3 rounded rect、4 triangle、5 heart、6 star、7 polygon、8 arrow（字符串名不会被解析，传整数）"))},
        {QStringLiteral("width"), prop(QStringLiteral("number"), QStringLiteral("1..1000000；静态基准值"))},
        {QStringLiteral("height"), prop(QStringLiteral("number"), QStringLiteral("1..1000000；静态基准值"))},
        {QStringLiteral("cornerRadius"), prop(QStringLiteral("number"), QStringLiteral("0..1000000；静态基准值"))},
        {QStringLiteral("sides"), prop(QStringLiteral("integer"), QStringLiteral("3..512（定义参数）"))},
        {QStringLiteral("innerRatio"), prop(QStringLiteral("number"), QStringLiteral("0..0.95"))},
        {QStringLiteral("strokeWidth"), prop(QStringLiteral("number"), QStringLiteral("0..1000；静态基准值"))},
        {QStringLiteral("fillColor"), prop(QStringLiteral("string"), QStringLiteral("CSS hex"))},
        {QStringLiteral("strokeColor"), prop(QStringLiteral("string"), QStringLiteral("CSS hex"))},
    });

    QJsonObject kind;
    kind.insert(QStringLiteral("type"), QStringLiteral("string"));
    kind.insert(QStringLiteral("enum"),
                QJsonArray::fromStringList(
                    {QStringLiteral("empty"), QStringLiteral("text"),
                     QStringLiteral("vector"), QStringLiteral("camera"),
                     QStringLiteral("asset")}));
    kind.insert(QStringLiteral("description"),
                QStringLiteral("内容类型；特殊内容类型已整类删除"));

    QJsonObject properties;
    properties.insert(QStringLiteral("kind"), kind);
    properties.insert(QStringLiteral("text"), text);
    properties.insert(QStringLiteral("vector"), vector);
    properties.insert(QStringLiteral("camera"),
                      prop(QStringLiteral("object"), QStringLiteral("kind=camera：空对象")));
    properties.insert(QStringLiteral("assetHash"),
                      prop(QStringLiteral("string"),
                           QStringLiteral("kind=asset：64 位 sha256；换素材用 asset.assign")));
    properties.insert(QStringLiteral("assetType"),
                      prop(QStringLiteral("string"),
                           QStringLiteral("kind=asset：image|video|audio|html")));
    properties.insert(QStringLiteral("sourceIn"),
                      prop(QStringLiteral("integer"), QStringLiteral("kind=asset：>= 0")));
    properties.insert(QStringLiteral("volume"),
                      prop(QStringLiteral("number"), QStringLiteral("kind=asset：>= 0；静态基准值，动画值写在 keyframes[].content")));
    properties.insert(QStringLiteral("fadeIn"),
                      prop(QStringLiteral("number"), QStringLiteral("kind=audio：秒")));
    properties.insert(QStringLiteral("fadeOut"),
                      prop(QStringLiteral("number"), QStringLiteral("kind=audio：秒")));

    QJsonObject schema;
    schema.insert(QStringLiteral("type"), QStringLiteral("object"));
    schema.insert(
        QStringLiteral("description"),
        QStringLiteral("实体静态内容。这里只放定义参数（文字内容/字体/颜色、"
                       "形状类型、素材引用等）；动画参数由整帧关键帧的 content "
                       "承载。")
            + animationParamsSummary()
            + QStringLiteral("。定义参数不能写进关键帧。"));
    schema.insert(QStringLiteral("properties"), properties);
    return schema;
}

QJsonObject schemaTopic(const QString& topic)
{
    if (topic == QLatin1String("project"))
    {
        return openSchema({
            {QStringLiteral("name"), prop(QStringLiteral("string"), QStringLiteral("1..256 字符"))},
            {QStringLiteral("width"), prop(QStringLiteral("integer"), QStringLiteral("16..16384"))},
            {QStringLiteral("height"), prop(QStringLiteral("integer"), QStringLiteral("16..16384"))},
            {QStringLiteral("fps"), prop(QStringLiteral("integer"), QStringLiteral("1..240"))},
            {QStringLiteral("background"), prop(QStringLiteral("string"), QStringLiteral("CSS hex"))},
        });
    }
    if (topic == QLatin1String("entity"))
    {
        return openSchema({
            {QStringLiteral("uuid"), prop(QStringLiteral("string"), QStringLiteral("UUID v4"))},
            {QStringLiteral("name"), prop(QStringLiteral("string"), QStringLiteral("1..256 字符"))},
            {QStringLiteral("parentUuid"), prop(QStringLiteral("string|null"), QStringLiteral("null = 根实体"))},
            {QStringLiteral("space"), prop(QStringLiteral("string"), QStringLiteral("parent|canvas；canvas 不继承父级 transform"))},
            {QStringLiteral("followParentRange"), prop(QStringLiteral("boolean"), QStringLiteral("true 时区间必须等于直接父区间"))},
            {QStringLiteral("start"), prop(QStringLiteral("integer"), QStringLiteral("全局帧号，>= 0"))},
            {QStringLiteral("end"), prop(QStringLiteral("integer"), QStringLiteral("全局帧号，>= start（闭区间，含 end）"))},
            {QStringLiteral("row"), prop(QStringLiteral("integer"), QStringLiteral("主时间线 >= 0；私有时间线（parentUuid 非空）允许负 row（-1000000..1000000），负 row 画在父实体后面"))},
            {QStringLiteral("hidden"), prop(QStringLiteral("boolean"), QString())},
            {QStringLiteral("locked"), prop(QStringLiteral("boolean"), QStringLiteral("锁定后 MCP 不能写"))},
            {QStringLiteral("inheritVisibility"), prop(QStringLiteral("boolean"), QString())},
            {QStringLiteral("visible"), prop(QStringLiteral("boolean"), QString())},
            {QStringLiteral("physicsInteract"), prop(QStringLiteral("boolean"), QStringLiteral("参与碰撞"))},
            {QStringLiteral("transform"), prop(QStringLiteral("object"), QStringLiteral("x/y/rotation/scaleX/scaleY/pivotX/pivotY/opacity（静态回退）"))},
            {QStringLiteral("content"), contentSchema()},
            {QStringLiteral("keyframes"), arrayOf(QStringLiteral("object"), QStringLiteral("整帧关键帧数组；每个对象含 frame/x/y/rotation/scaleX/scaleY/opacity/visible/physicsInteract/content/htmlVars/easing/note，没有 track。") + animationParamsSummary())},
            {QStringLiteral("solvers"), arrayOf(QStringLiteral("object"), QStringLiteral("求解段，字段同 solver.create 去掉 entityUuid"))},
        });
    }
    if (topic == QLatin1String("keyframes"))
    {
        return openSchema({
            {QStringLiteral("frame"), prop(QStringLiteral("integer"), QStringLiteral("相对实体 start，0..duration-1"))},
            {QStringLiteral("x"), prop(QStringLiteral("number"), QStringLiteral("必填；整帧关键帧包含全部动画参数"))},
            {QStringLiteral("y"), prop(QStringLiteral("number"), QStringLiteral("必填"))},
            {QStringLiteral("rotation"), prop(QStringLiteral("number"), QStringLiteral("必填，度"))},
            {QStringLiteral("scaleX"), prop(QStringLiteral("number"), QStringLiteral("必填"))},
            {QStringLiteral("scaleY"), prop(QStringLiteral("number"), QStringLiteral("必填"))},
            {QStringLiteral("opacity"), prop(QStringLiteral("number"), QStringLiteral("必填，0..1"))},
            {QStringLiteral("visible"), prop(QStringLiteral("boolean"), QStringLiteral("必填，步骤值"))},
            {QStringLiteral("physicsInteract"), prop(QStringLiteral("boolean"), QStringLiteral("必填，步骤值"))},
            {QStringLiteral("content"), prop(QStringLiteral("object"), QStringLiteral("必填；该内容类型的全部动画参数。") + animationParamsSummary() + QStringLiteral("。定义参数不能写进来"))},
            {QStringLiteral("htmlVars"), prop(QStringLiteral("object"), QStringLiteral("HTML manifest 数字变量覆盖值；非数字变量不进关键帧"))},
            {QStringLiteral("easing"), prop(QStringLiteral("string|object"), QStringLiteral("缓动名（大小写不敏感）：") + easingWhitelist() + QStringLiteral("；别名 easein/easeout/easeinout/step 归一化为 easeinquad/easeoutquad/easeinoutquad/instant；也可用 {type:bezier,inX,inY,outX,outY}"))},
            {QStringLiteral("note"), prop(QStringLiteral("string"), QStringLiteral("最长 4096"))},
        });
    }
    if (topic == QLatin1String("asset"))
    {
        return openSchema({
            {QStringLiteral("hash"), prop(QStringLiteral("string"), QStringLiteral("64 位 sha256"))},
            {QStringLiteral("type"), prop(QStringLiteral("string"), QStringLiteral("image|video|audio|html"))},
            {QStringLiteral("name"), prop(QStringLiteral("string"), QString())},
            {QStringLiteral("width"), prop(QStringLiteral("integer"), QString())},
            {QStringLiteral("height"), prop(QStringLiteral("integer"), QString())},
            {QStringLiteral("durationFrames"), prop(QStringLiteral("integer"), QStringLiteral("视频/音频帧数"))},
            {QStringLiteral("sampleRate"), prop(QStringLiteral("integer"), QStringLiteral("仅音频"))},
            {QStringLiteral("channels"), prop(QStringLiteral("integer"), QStringLiteral("仅音频"))},
            {QStringLiteral("entry"), prop(QStringLiteral("string|null"), QStringLiteral("仅 HTML"))},
            {QStringLiteral("variables"), prop(QStringLiteral("object|null"), QStringLiteral("仅 HTML"))},
            {QStringLiteral("usedBy"), arrayOf(QStringLiteral("string"), QString())},
            {QStringLiteral("missing"), prop(QStringLiteral("boolean"), QString())},
            {QStringLiteral("path"), prop(QStringLiteral("string"), QStringLiteral("服务器本地路径"))},
        });
    }
    if (topic == QLatin1String("markers"))
    {
        return openSchema({
            {QStringLiteral("id"), prop(QStringLiteral("string"), QStringLiteral("省略 = 新建"))},
            {QStringLiteral("frame"), prop(QStringLiteral("integer"), QStringLiteral("全局帧号"))},
            {QStringLiteral("title"), prop(QStringLiteral("string"), QStringLiteral("1..256 字符"))},
            {QStringLiteral("note"), prop(QStringLiteral("string"), QStringLiteral("最长 4096"))},
            {QStringLiteral("tags"), arrayOf(QStringLiteral("string"), QStringLiteral("最多 32 个"))},
            {QStringLiteral("color"), prop(QStringLiteral("string"), QStringLiteral("CSS hex"))},
        });
    }
    if (topic == QLatin1String("errors"))
    {
        QJsonArray codes;
        for (const QString& code :
             {QStringLiteral("invalid_argument"), QStringLiteral("conflict"),
              QStringLiteral("internal_error"), QStringLiteral("limit_exceeded"),
              QStringLiteral("confirm_required"), QStringLiteral("not_available"),
              QStringLiteral("permission_denied"),
              QStringLiteral("stopped_by_human"),
              QStringLiteral("request_timeout")})
            codes.append(code);
        QJsonObject schema;
        schema.insert(QStringLiteral("codes"), codes);
        schema.insert(QStringLiteral("detailsKeys"),
                      QJsonArray::fromStringList(
                          {QStringLiteral("allowedRange"),
                           QStringLiteral("allowedFields"),
                           QStringLiteral("startFrame"), QStringLiteral("missing"),
                           QStringLiteral("index"), QStringLiteral("lockedBy"),
                           QStringLiteral("conflicts"), QStringLiteral("requires"),
                           QStringLiteral("affected"), QStringLiteral("reason"),
                           QStringLiteral("path")}));
        return schema;
    }
    if (topic == QLatin1String("solver"))
    {
        return openSchema({
            {QStringLiteral("entityUuid"), prop(QStringLiteral("string"), QStringLiteral("目标实体 UUID"))},
            {QStringLiteral("solverId"), prop(QStringLiteral("string"), QStringLiteral("求解段 id"))},
            {QStringLiteral("effect"), prop(QStringLiteral("string"), QStringLiteral("gravity|orbit|spin|blackhole|spiral"))},
            {QStringLiteral("startFrame"), prop(QStringLiteral("integer"), QStringLiteral("相对实体 start，0..duration-1"))},
            {QStringLiteral("endFrame"), prop(QStringLiteral("integer"), QStringLiteral("-1 = 自动（物理停止/碰撞/片段末尾）；>=0 = 显式结束帧，startFrame..duration-1"))},
            {QStringLiteral("initialVelocity"), prop(QStringLiteral("object"), QStringLiteral("{x,y} px/s，仅 gravity/blackhole"))},
            {QStringLiteral("params"), prop(QStringLiteral("object"), QStringLiteral("按效果取字段；gravity/orbit/blackhole/spiral 必填 targetUuid"))},
            {QStringLiteral("patch"), prop(QStringLiteral("object"), QStringLiteral("startFrame/endFrame/initialVelocity/params 子集"))},
            {QStringLiteral("frames"), arrayOf(QStringLiteral("integer"), QStringLiteral("相对帧号，最多 10000 个采样"))},
        });
    }
    if (topic == QLatin1String("preview"))
    {
        return openSchema({
            {QStringLiteral("frame"), prop(QStringLiteral("integer"), QStringLiteral("render_frame：全局帧号，默认播放头"))},
            {QStringLiteral("frames"), arrayOf(QStringLiteral("integer"), QStringLiteral("render_range：帧号数组，去重保序，最多 16 个；与 from/to 二选一"))},
            {QStringLiteral("from"), prop(QStringLiteral("integer"), QStringLiteral("render_range：闭区间起始帧"))},
            {QStringLiteral("to"), prop(QStringLiteral("integer"), QStringLiteral("render_range：闭区间结束帧"))},
            {QStringLiteral("count"), prop(QStringLiteral("integer"), QStringLiteral("render_range：均匀采样数量，1..16，默认 6"))},
            {QStringLiteral("cols"), prop(QStringLiteral("integer"), QStringLiteral("render_range：每行列数 1..6"))},
            {QStringLiteral("cameraUuid"), prop(QStringLiteral("string|null"), QStringLiteral("默认 view.state 摄像机；null = 全画布"))},
            {QStringLiteral("width"), prop(QStringLiteral("integer"), QStringLiteral("render_frame 1..4096；render_range 单格 64..1024"))},
            {QStringLiteral("height"), prop(QStringLiteral("integer"), QStringLiteral("render_frame：1..4096，默认工程高度"))},
            {QStringLiteral("background"), prop(QStringLiteral("string"), QStringLiteral("CSS 颜色"))},
            {QStringLiteral("includeBase64"),
             prop(QStringLiteral("boolean"), QStringLiteral("在返回 JSON 里附带 PNG base64；不传则只给临时 path"))},
            {QStringLiteral("showFrameLabel"), prop(QStringLiteral("boolean"), QStringLiteral("render_range：默认 true，每格叠加帧号角标"))},
        });
    }
    if (topic == QLatin1String("view"))
    {
        return openSchema({
            {QStringLiteral("playhead"), prop(QStringLiteral("integer"), QStringLiteral("全局帧号"))},
            {QStringLiteral("selection"), prop(QStringLiteral("string|null"), QStringLiteral("选中实体 UUID"))},
            {QStringLiteral("timelineParent"), prop(QStringLiteral("string|null"), QStringLiteral("空 = 根时间轴"))},
            {QStringLiteral("cameraUuid"), prop(QStringLiteral("string|null"), QStringLiteral("空 = 全画布"))},
            {QStringLiteral("viewRect"), prop(QStringLiteral("object|null"), QStringLiteral("{x,y,width,height} 项目坐标"))},
            {QStringLiteral("fps"), prop(QStringLiteral("integer"), QString())},
            {QStringLiteral("width"), prop(QStringLiteral("integer"), QString())},
            {QStringLiteral("height"), prop(QStringLiteral("integer"), QString())},
            {QStringLiteral("root"), prop(QStringLiteral("string"), QStringLiteral("工程目录"))},
        });
    }
    return QJsonObject();
}

const QStringList& schemaTopics()
{
    static const QStringList topics = {
        QStringLiteral("project"),   QStringLiteral("entity"),
        QStringLiteral("keyframes"), QStringLiteral("asset"),
        QStringLiteral("markers"),   QStringLiteral("errors"),
        QStringLiteral("solver"),    QStringLiteral("preview"),
        QStringLiteral("view"),
    };
    return topics;
}

QString guideMarkdown(const QString& topic)
{
    if (topic == QLatin1String("authoring"))
        return QStringLiteral(
            "# 编写规则\n"
            "- 先读 `project.summary` 和 `entity.list`，再改。\n"
            "- 实体时间用全局帧闭区间 `[start,end]`（含 end 帧，时长 `end-start+1`）；关键帧帧号相对实体 start，范围 `0..duration-1`。\n"
            "- 一次工具调用只做一个逻辑操作；有文本 diff 的写工具各自产生一个 commit。\n"
            "- git 只管理文本；素材不进 git，纯素材导入可能没有 commit。\n"
            "- 没有启用 Git 时 MCP 只读；写工具返回 `permission_denied`（details.reason=`git_disabled`），不要重试，先让人类启用 Git。\n"
            "- 批量工具全成功或全回滚。\n"
            "- 锁定实体和祖先不能写；MCP 不能解锁。\n"
            "- 删除实体/素材必须带 `confirm: true`。\n"
            "- 整帧关键帧：一个关键帧 = 该帧的全部动画参数 + 一个整体缓动 + 可选注释。没有 `track`，没有单参数轨道；`keyframes.set` 每个对象必须含 `frame,x,y,rotation,scaleX,scaleY,opacity,visible,physicsInteract`，以及该内容类型的全部 `content` 动画参数。缺多个动画参数时一次返回 `details.missingFields` 与 `details.requiredFields`。\n")
            + QStringLiteral("- 缓动名大小写不敏感；完整白名单：") + easingWhitelist()
            + QStringLiteral("（别名 easein/easeout/easeinout/step 分别归一化为 "
                             "easeinquad/easeoutquad/easeinoutquad/instant）。\n")
            + QStringLiteral(
            "- 动画参数：文本 `pixelSize/lineHeight/letterSpacing/padding/boxWidth/boxHeight/strokeWidth`；矢量 `width/height/cornerRadius/strokeWidth`；音频 `volume`；image/video/html/camera/empty 无动画参数（`content` 可省略或传 `{}`，HTML 数字变量写进 `htmlVars`）。定义参数（文字内容、字体、颜色、形状类型、素材引用等）不能写进关键帧。\n"
            "- 改单个字段用 `keyframes.upsert`：服务端先按该帧求值补全全部动画参数，再覆盖传入字段。\n"
            "- 改完用 `entity.get` 或 `keyframes.get` 自查。\n");
    if (topic == QLatin1String("workflow"))
        return QStringLiteral(
            "# 推荐工作流\n"
            "1. `project.summary` 确认 fps/尺寸/内容长度；fps/尺寸创建后锁定，`project.update` 不能改。\n"
            "2. `entity.list` / `entity.get` 找到目标 UUID、父级、row。\n"
            "3. `entity.createTree` 一次建父子层级，或 `entity.createMany` 建平级实体。\n"
            "4. `keyframes.setMany` 一次写多个实体的整帧关键帧（没有 track）。\n"
            "5. `entity.move` / `entity.trim` / `entity.reparent` 调整时间与层级。\n"
            "6. 需要回退时用 `history.undo`（内存撤销，不产生 git commit），不要写相反值模拟撤销。\n");
    if (topic == QLatin1String("solver"))
        return QStringLiteral(
            "# 求解段\n"
            "- 段帧号相对实体 start，闭区间 `[startFrame,endFrame]`；`endFrame=-1`（默认）= 自动：解到第一次物理停止/碰撞，没有停止则到片段末帧。\n"
            "- `endFrame>=0` 是高级显式覆盖：解到该帧并冻结；物理上更早停止时采样仍保留该区间，但 `stopped` 只在真实停止帧为真。\n"
            "- 停止原因分 `collision`（碰撞/落入事件视界）、`stopThreshold`（低速阈值）、`clipEnd`（片段末尾）、`explicitEnd`（显式结束帧）。\n"
            "- `gravity`/`orbit`/`blackhole`/`spiral` 必填 `params.targetUuid`；`spin` 不需要目标。\n"
            "- 求解起点必须是一个普通整帧关键帧：`startFrame` 上要有 `keyframes` 数组里的整帧关键帧。\n"
            "- 段不能重叠；显式 `endFrame` 覆盖的 `startFrame < f <= endFrame`（含结束帧）内不能写普通整帧关键帧。自动段（`endFrame=-1`）由起点后的第一个普通整帧关键帧 N 结束于 N-1。\n"
            "- `solver.evaluate` 只读，返回相对 start 的采样点、速度和碰撞/停止标记，不写关键帧；`summary.stoppedFrame` 是第一次物理停止。\n");
    if (topic == QLatin1String("preview"))
        return QStringLiteral(
            "# 预览\n"
            "- `view.state` 读取播放头、选中实体、私有时间线、活动摄像机和视口范围。\n"
            "- `preview.render_frame` 渲染指定全局帧并写入临时 PNG；默认帧是播放头、默认摄像机来自 `view.state`。\n"
            "- `preview.render_range` 一次渲染多帧并拼成网格接触表 PNG：帧选择用 `frames` 或 `from`+`to`+`count` 二选一；"
            "`cols`/`width`/`showFrameLabel` 控制排版，整张只按 1 次预览限流计算。\n"
            "- 输出宽高限制 1..4096，PNG 不超过 20MB；预览不修改工程、不产生 commit。\n"
            "- 返回 `path` 后可直接读取图片；带 `includeBase64: true` 时 HTTP 返回还会带标准 MCP image content block。\n"
            "- 渲染失败返回 `internal_error`。\n");
    if (topic == QLatin1String("assets"))
        return QStringLiteral(
            "# 素材\n"
            "- `asset.import` 只接受本地绝对路径；相同内容按 sha256 去重。\n"
            "- 工程目录内的路径可直接导入；工程目录外（或从工程内软链到工程外）会弹出 PVForge 窗口对话框，由人类确认后才复制。AI 不能自行确认，也没有 `confirm` 参数；没有人类可确认时返回 `confirm_required`（details.path = 规范化后的路径）。\n"
            "- 密钥、浏览器配置和系统目录（如 `~/.ssh`、`~/.config`、`/etc`）一律拒绝，返回 `permission_denied`。\n"
            "- 素材存在 `assets/`，不进 git；纯素材导入没有文本 diff 时不产生 commit。\n"
            "- 视频默认拆出 WAV 音频素材，返回 `asset` 与 `audioAsset` 两个 hash。\n"
            "- HTML 必须两步：先 `asset.import` 导入 `.pvhtml`，再 `entity.create`/`asset.assign`。\n"
            "- 素材没有 UUID，只有 hash；删除素材不会删除引用它的实体。\n");
    if (topic == QLatin1String("examples"))
        return QStringLiteral(
            "# 示例\n"
            "```json\n"
            "{\"root\":{\"name\":\"标题组\",\"start\":0,\"end\":90,"
            "\"children\":[{\"name\":\"标题\",\"content\":{\"kind\":\"text\","
            "\"text\":{\"text\":\"PVFORGE\",\"pixelSize\":180}}}]}}\n"
            "```\n"
            "```json\n"
            "{\"uuid\":\"<uuid>\",\"keyframes\":["
            "{\"frame\":0,\"x\":0,\"y\":0,\"rotation\":0,\"scaleX\":1,"
            "\"scaleY\":1,\"opacity\":1,\"visible\":true,"
            "\"physicsInteract\":false,"
            "\"content\":{\"pixelSize\":96,\"lineHeight\":1.0,"
            "\"letterSpacing\":0,\"padding\":6,\"boxWidth\":0,\"boxHeight\":0,"
            "\"strokeWidth\":0},\"htmlVars\":{},\"easing\":\"linear\","
            "\"note\":\"\"}]}\n"
            "```\n"
            "对所有关键帧调用 `keyframes.setMany`，帧号按各实体自己的 start 计算；"
            "只改一个字段时用 `keyframes.upsert`。\n");
    if (topic == QLatin1String("pitfalls"))
        return QStringLiteral(
            "# 高频坑点（与当前实现一致）\n"
            "- `entity.update` 的 `patch.content` 必须用嵌套写法：text → `content.text{...}`，vector → `content.vector{...}`，asset → `content.assetHash/assetType/sourceIn/volume/fadeIn/fadeOut`。把 `shape`、`width` 等扁平字段放在 `content` 顶层会返回 `invalid_argument`（details.expectedContentPath / details.unexpectedKeys），不会再静默丢字段；换素材引用请用 `asset.assign`。\n"
            "- `content.assetType` 只是声明值：`entity.create` 和 `asset.assign` 都会用素材库里的真实类型覆盖它，写错会被改回，也不能靠它把图片当音频。换素材前先 `asset.get` 看 `type`。\n"
            "- `asset.import` 的 `audioAsset` 是素材对象（与 `asset` 同结构），视频默认拆出 WAV 时非 null；`extractAudio:false` 或非视频时为 `null`。不要按布尔值或 hash 字符串来读。\n"
            "- `fadeIn` / `fadeOut` 是 asset 内容的通用字段：任意素材类型都能写，单位秒、范围 0..36000，只有音频混音真正生效；视觉素材写了不会报错。\n"
            "- `project.update` 只能改 `name` / `background`；`fps`、`width`、`height` 创建后锁定，传了返回 `conflict`（不是静默忽略）。`confirm` 不是合法参数，传了返回 `invalid_argument`。\n"
            "- `row` 规则：主时间线（`parentUuid` 为空）必须 `row >= 0`；私有时间线允许负 `row`，可把子实体放到父实体下层。reparent 回主时间线时不能留负 row。\n"
            "- `keyframes.setMany` 的格式是 `{items:[{uuid,keyframes:[整帧...]}]}`。每个整帧必须含 `frame,x,y,rotation,scaleX,scaleY,opacity,visible,physicsInteract` 和该内容类型的全部动画参数，`frame` 相对实体 start、范围 `0..duration-1`，没有 `track`。缺字段会一次返回全部 `details.missingFields`；只改单个字段用 `keyframes.upsert`。\n"
            "- 动画参数按内容类型：text `pixelSize/lineHeight/letterSpacing/padding/boxWidth/boxHeight/strokeWidth`；vector `width/height/cornerRadius/strokeWidth`；audio `volume`；image/video/html/camera/empty 无（`content` 可省略或传 `{}`，HTML 数字变量写进 `htmlVars`）。\n"
            "- `asset.assign` 会冲突：camera 不能换素材；text/vector 必须先用 `entity.update` 把 `patch.content` 改成 `{\"kind\":\"empty\"}`，再 assign；音频与 image/video/html 互不兼容。实体时长超过素材剩余长度时会自动裁剪实体，若会删除子实体则返回 `conflict`。\n"
            "- 工程未打开时所有工具返回 `not_available`；Git 未启用时写工具返回 `permission_denied`（details.reason=`git_disabled`），只读工具仍可用，不要重试写操作。\n"
            "- 单次工具执行超过 15 秒会返回 `request_timeout` 并回滚，不留下半成品。\n"
            "- 删除实体/素材必须带 `confirm: true`；`entity.trim` 会级联裁剪子实体，删子实体同样需要 confirm。\n");
    return QString();
}

} // namespace

Result schemaGet(Tx& tx, const QJsonObject& args)
{
    Q_UNUSED(tx);
    Result error;
    QString topic = QStringLiteral("all");
    if (!readString(args, "topic", &topic, &error))
        return error;

    if (topic != QLatin1String("all") && !schemaTopics().contains(topic))
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("未知 schema topic：%1").arg(topic));

    QJsonObject schema;
    if (topic == QLatin1String("all"))
    {
        for (const QString& name : schemaTopics())
            schema.insert(name, schemaTopic(name));
    }
    else
    {
        schema = schemaTopic(topic);
    }

    QJsonObject data;
    data.insert(QStringLiteral("version"), 1);
    data.insert(QStringLiteral("topic"), topic);
    data.insert(QStringLiteral("schema"), schema);
    return success(data);
}

Result guideGet(Tx& tx, const QJsonObject& args)
{
    Q_UNUSED(tx);
    Result error;
    QString topic = QStringLiteral("authoring");
    if (!readString(args, "topic", &topic, &error))
        return error;

    static const QStringList topics = {
        QStringLiteral("authoring"), QStringLiteral("workflow"),
        QStringLiteral("solver"),    QStringLiteral("preview"),
        QStringLiteral("assets"),    QStringLiteral("examples"),
        QStringLiteral("pitfalls"),
    };
    if (!topics.contains(topic))
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("未知 guide topic：%1").arg(topic));

    QJsonObject data;
    data.insert(QStringLiteral("topic"), topic);
    data.insert(QStringLiteral("markdown"), guideMarkdown(topic));
    return success(data);
}

QJsonObject listTools()
{
    const QJsonObject empty = objectSchema({});
    const QJsonObject uuid = objectSchema(
        {{QStringLiteral("uuid"), prop(QStringLiteral("string"), QStringLiteral("实体 UUID"))}},
        {QStringLiteral("uuid")});

    QJsonArray tools;
    tools.append(toolSchema(QStringLiteral("project.get"),
                            QStringLiteral("读取工程设置、路径、计数和 git 状态"), empty));
    tools.append(toolSchema(QStringLiteral("project.summary"),
                            QStringLiteral("轻量工程概况"), empty));
    tools.append(toolSchema(
        QStringLiteral("project.update"),
        QStringLiteral("修改工程名称/背景；fps 与分辨率创建后锁定"),
        objectSchema({{QStringLiteral("name"), prop(QStringLiteral("string"), QString())},
                      {QStringLiteral("width"), prop(QStringLiteral("integer"), QStringLiteral("创建后锁定，传了会被拒绝"))},
                      {QStringLiteral("height"), prop(QStringLiteral("integer"), QStringLiteral("创建后锁定，传了会被拒绝"))},
                      {QStringLiteral("fps"), prop(QStringLiteral("integer"), QStringLiteral("创建后锁定，传了会被拒绝"))},
                      {QStringLiteral("background"), prop(QStringLiteral("string"), QString())}})));

    const QJsonObject entityCreateSchema = objectSchema(
        {{QStringLiteral("name"), prop(QStringLiteral("string"), QString())},
         {QStringLiteral("parentUuid"), prop(QStringLiteral("string|null"), QString())},
         {QStringLiteral("space"), prop(QStringLiteral("string"), QStringLiteral("parent|canvas"))},
         {QStringLiteral("followParentRange"), prop(QStringLiteral("boolean"), QStringLiteral("true 时区间必须等于直接父区间"))},
         {QStringLiteral("start"), prop(QStringLiteral("integer"), QStringLiteral("必填"))},
         {QStringLiteral("end"), prop(QStringLiteral("integer"), QStringLiteral("默认按内容类型"))},
         {QStringLiteral("row"), prop(QStringLiteral("integer"), QStringLiteral("默认最高占用 +1；根时间线 >= 0，私有时间线可为负"))},
         {QStringLiteral("content"), prop(QStringLiteral("object"), QStringLiteral("kind: empty|text|vector|camera|asset；定义参数见 schema.get entity，动画参数见 keyframes"))},
         {QStringLiteral("transform"), prop(QStringLiteral("object"), QString())},
         {QStringLiteral("hidden"), prop(QStringLiteral("boolean"), QString())},
         {QStringLiteral("locked"), prop(QStringLiteral("boolean"), QString())},
         {QStringLiteral("inheritVisibility"), prop(QStringLiteral("boolean"), QString())},
         {QStringLiteral("visible"), prop(QStringLiteral("boolean"), QString())},
         {QStringLiteral("physicsInteract"), prop(QStringLiteral("boolean"), QString())},
         {QStringLiteral("keyframes"), arrayOf(QStringLiteral("object"), QStringLiteral("整帧关键帧数组；每个对象必须含全部动画参数，没有 track"))},
         {QStringLiteral("solvers"), arrayOf(QStringLiteral("object"), QStringLiteral("求解段，字段同 solver.create 去掉 entityUuid"))}},
        {QStringLiteral("start")});

    tools.append(toolSchema(QStringLiteral("entity.create"),
                            QStringLiteral("创建一个实体，必须显式给 start"),
                            entityCreateSchema));
    tools.append(toolSchema(
        QStringLiteral("entity.createTree"), QStringLiteral("一次创建父实体、子实体、关键帧和行号"),
        objectSchema({{QStringLiteral("parentUuid"), prop(QStringLiteral("string|null"), QString())},
                      {QStringLiteral("root"), prop(QStringLiteral("object"), QStringLiteral("递归节点，最多 200 个"))}},
                     {QStringLiteral("root")})));
    tools.append(toolSchema(
        QStringLiteral("entity.createMany"), QStringLiteral("一次创建多个互不嵌套的实体"),
        objectSchema({{QStringLiteral("entities"), arrayOf(QStringLiteral("object"), QStringLiteral("1..200 个"))}},
                     {QStringLiteral("entities")})));
    tools.append(toolSchema(
        QStringLiteral("entity.get"),
        QStringLiteral("读取实体完整状态（整帧关键帧数组）和指定帧求值（默认播放头，含求解段）"),
        objectSchema({{QStringLiteral("uuid"), prop(QStringLiteral("string"), QString())},
                      {QStringLiteral("frame"), prop(QStringLiteral("integer"), QStringLiteral("默认当前播放头"))},
                      {QStringLiteral("includeChildren"), prop(QStringLiteral("boolean"), QString())}},
                     {QStringLiteral("uuid")})));
    tools.append(toolSchema(
        QStringLiteral("entity.list"),
        QStringLiteral("按父级/根层分页列出实体；tree=true 时一次返回完整嵌套树"),
        objectSchema({{QStringLiteral("parentUuid"), prop(QStringLiteral("string|null"), QStringLiteral("省略 = 全部；tree=true 时省略 = 从所有顶层实体开始"))},
                      {QStringLiteral("offset"), prop(QStringLiteral("integer"), QStringLiteral("仅扁平模式分页用"))},
                      {QStringLiteral("limit"), prop(QStringLiteral("integer"), QStringLiteral("1..500；仅扁平模式分页用"))},
                      {QStringLiteral("includeKeyframes"), prop(QStringLiteral("boolean"), QStringLiteral("返回整帧关键帧数组"))},
                      {QStringLiteral("includeSolvers"), prop(QStringLiteral("boolean"), QString())},
                      {QStringLiteral("tree"), prop(QStringLiteral("boolean"), QStringLiteral("默认 false；true 时从根节点递归返回 children 嵌套树"))}})));
    tools.append(toolSchema(
        QStringLiteral("entity.update"), QStringLiteral("局部修改实体（不改时间/父级/关键帧/素材引用）"),
        objectSchema({{QStringLiteral("uuid"), prop(QStringLiteral("string"), QString())},
                      {QStringLiteral("patch"), prop(QStringLiteral("object"), QString())}},
                     {QStringLiteral("uuid"), QStringLiteral("patch")})));
    tools.append(toolSchema(
        QStringLiteral("entity.updateMany"), QStringLiteral("批量局部修改，整批一个 commit"),
        objectSchema({{QStringLiteral("items"), arrayOf(QStringLiteral("object"), QStringLiteral("1..200 个 {uuid,patch}"))}},
                     {QStringLiteral("items")})));
    tools.append(toolSchema(
        QStringLiteral("entity.move"),
        QStringLiteral("平移实体/子树或仅改行号；start/delta 二选一，row 可单独给"),
        objectSchema({{QStringLiteral("uuid"), prop(QStringLiteral("string"), QString())},
                      {QStringLiteral("start"), prop(QStringLiteral("integer"), QStringLiteral("与 delta 二选一"))},
                      {QStringLiteral("delta"), prop(QStringLiteral("integer"), QStringLiteral("与 start 二选一"))},
                      {QStringLiteral("row"), prop(QStringLiteral("integer"), QStringLiteral("主时间线 >= 0；私有时间线可为负"))}},
                     {QStringLiteral("uuid")})));
    tools.append(toolSchema(
        QStringLiteral("entity.trim"), QStringLiteral("裁剪实体并级联裁剪子实体；删子实体需 confirm"),
        objectSchema({{QStringLiteral("uuid"), prop(QStringLiteral("string"), QString())},
                      {QStringLiteral("start"), prop(QStringLiteral("integer"), QString())},
                      {QStringLiteral("end"), prop(QStringLiteral("integer"), QString())},
                      {QStringLiteral("confirm"), prop(QStringLiteral("boolean"), QString())}},
                     {QStringLiteral("uuid")})));
    tools.append(toolSchema(
        QStringLiteral("entity.reparent"),
        QStringLiteral("换父级；preserveWorld 拒绝带整帧关键帧的实体；"
                       "引用该实体的求解段会列入 confirm_required"),
        objectSchema({{QStringLiteral("uuid"), prop(QStringLiteral("string"), QString())},
                      {QStringLiteral("parentUuid"), prop(QStringLiteral("string|null"), QString())},
                      {QStringLiteral("mode"), prop(QStringLiteral("string"), QStringLiteral("preserveWorld|keepLocal"))},
                      {QStringLiteral("row"), prop(QStringLiteral("integer"), QStringLiteral("主时间线 >= 0；私有时间线可为负"))},
                      {QStringLiteral("confirm"), prop(QStringLiteral("boolean"), QStringLiteral("有求解段依赖时必须为 true"))}},
                     {QStringLiteral("uuid"), QStringLiteral("parentUuid"), QStringLiteral("mode")})));
    tools.append(toolSchema(
        QStringLiteral("entity.duplicate"), QStringLiteral("复制实体/子树，生成新 UUID"),
        objectSchema({{QStringLiteral("uuid"), prop(QStringLiteral("string"), QString())},
                      {QStringLiteral("deep"), prop(QStringLiteral("boolean"), QString())},
                      {QStringLiteral("offsetFrames"), prop(QStringLiteral("integer"), QString())},
                      {QStringLiteral("parentUuid"), prop(QStringLiteral("string|null"), QString())},
                      {QStringLiteral("name"), prop(QStringLiteral("string"), QString())},
                      {QStringLiteral("row"), prop(QStringLiteral("integer"), QStringLiteral("主时间线 >= 0；私有时间线可为负"))}},
                     {QStringLiteral("uuid")})));
    tools.append(toolSchema(
        QStringLiteral("entity.delete"),
        QStringLiteral("删除实体及后代，必须 confirm；依赖求解段列入 affected.solverDependencies"),
        objectSchema({{QStringLiteral("uuid"), prop(QStringLiteral("string"), QString())},
                      {QStringLiteral("confirm"), prop(QStringLiteral("boolean"), QStringLiteral("必须为 true"))}},
                     {QStringLiteral("uuid"), QStringLiteral("confirm")})));
    tools.append(toolSchema(
        QStringLiteral("entity.deleteMany"), QStringLiteral("批量删除，必须 confirm"),
        objectSchema({{QStringLiteral("uuids"), arrayOf(QStringLiteral("string"), QStringLiteral("1..200 个"))},
                      {QStringLiteral("confirm"), prop(QStringLiteral("boolean"), QString())}},
                     {QStringLiteral("uuids"), QStringLiteral("confirm")})));

    const QJsonObject keyframesSetSchema = objectSchema(
        {{QStringLiteral("uuid"), prop(QStringLiteral("string"), QString())},
         {QStringLiteral("keyframes"), arrayOf(QStringLiteral("object"), QStringLiteral("整组替换；每个对象必须含该实体类型的全部动画参数，缺字段报错。") + animationParamsSummary())}},
        {QStringLiteral("uuid"), QStringLiteral("keyframes")});
    tools.append(toolSchema(
        QStringLiteral("keyframes.get"),
        QStringLiteral("读取实体的完整整帧关键帧数组"),
        objectSchema({{QStringLiteral("uuid"), prop(QStringLiteral("string"), QString())}},
                     {QStringLiteral("uuid")})));
    tools.append(toolSchema(
        QStringLiteral("keyframes.set"),
        QStringLiteral("严格整组替换整帧关键帧；每个关键帧必须含全部动画参数，没有 track。")
            + animationParamsSummary(),
        keyframesSetSchema));
    tools.append(toolSchema(
        QStringLiteral("keyframes.setMany"),
        QStringLiteral("一次替换多个实体的整帧关键帧，整批一个 commit。")
            + animationParamsSummary(),
        objectSchema({{QStringLiteral("items"), arrayOf(QStringLiteral("object"), QStringLiteral("1..200 个 {uuid,keyframes}；每个 keyframe 的要求同 keyframes.set。") + animationParamsSummary())}},
                     {QStringLiteral("items")})));
    tools.append(toolSchema(
        QStringLiteral("keyframes.upsert"),
        QStringLiteral("按帧写入部分字段：先求值补全全部动画参数，再覆盖传入字段"),
        objectSchema({{QStringLiteral("uuid"), prop(QStringLiteral("string"), QString())},
                      {QStringLiteral("frame"), prop(QStringLiteral("integer"), QStringLiteral("0..duration-1"))},
                      {QStringLiteral("partial"), prop(QStringLiteral("object"), QStringLiteral("x/y/rotation/scaleX/scaleY/opacity/visible/physicsInteract/content/htmlVars/easing/note 的子集"))}},
                     {QStringLiteral("uuid"), QStringLiteral("frame")})));
    tools.append(toolSchema(
        QStringLiteral("keyframes.remove"),
        QStringLiteral("按帧删除整帧关键帧（幂等）；求解段起点不能删除"),
        objectSchema({{QStringLiteral("uuid"), prop(QStringLiteral("string"), QString())},
                      {QStringLiteral("frames"), arrayOf(QStringLiteral("integer"), QStringLiteral("要删除的帧号"))}},
                     {QStringLiteral("uuid"), QStringLiteral("frames")})));
    tools.append(toolSchema(
        QStringLiteral("keyframes.move"),
        QStringLiteral("移动一个整帧关键帧到另一帧"),
        objectSchema({{QStringLiteral("uuid"), prop(QStringLiteral("string"), QString())},
                      {QStringLiteral("fromFrame"), prop(QStringLiteral("integer"), QString())},
                      {QStringLiteral("toFrame"), prop(QStringLiteral("integer"), QString())},
                      {QStringLiteral("overwrite"), prop(QStringLiteral("boolean"), QString())}},
                     {QStringLiteral("uuid"), QStringLiteral("fromFrame"),
                      QStringLiteral("toFrame")})));
    tools.append(toolSchema(
        QStringLiteral("keyframes.clear"), QStringLiteral("清空整帧关键帧"),
        objectSchema({{QStringLiteral("uuid"), prop(QStringLiteral("string"), QString())}},
                     {QStringLiteral("uuid")})));
    tools.append(toolSchema(
        QStringLiteral("keyframes.setEasing"), QStringLiteral("修改已有关键帧的整帧缓动"),
        objectSchema({{QStringLiteral("uuid"), prop(QStringLiteral("string"), QString())},
                      {QStringLiteral("frame"), prop(QStringLiteral("integer"), QString())},
                      {QStringLiteral("easing"), prop(QStringLiteral("string|object"), QString())}},
                     {QStringLiteral("uuid"),
                      QStringLiteral("frame"), QStringLiteral("easing")})));

    tools.append(toolSchema(
        QStringLiteral("solver.create"), QStringLiteral("给实体新增一个求解段"),
        objectSchema({{QStringLiteral("entityUuid"), prop(QStringLiteral("string"), QStringLiteral("实体 UUID"))},
                      {QStringLiteral("effect"), prop(QStringLiteral("string"), QStringLiteral("gravity|orbit|spin|blackhole|spiral"))},
                      {QStringLiteral("startFrame"), prop(QStringLiteral("integer"), QStringLiteral("相对实体 start"))},
                      {QStringLiteral("endFrame"), prop(QStringLiteral("integer"), QStringLiteral("默认 -1（自动停止/片段末尾）"))},
                      {QStringLiteral("initialVelocity"), prop(QStringLiteral("object"), QStringLiteral("{x,y} px/s"))},
                      {QStringLiteral("params"), prop(QStringLiteral("object"), QStringLiteral("按效果取字段"))}},
                     {QStringLiteral("entityUuid"), QStringLiteral("effect"),
                      QStringLiteral("startFrame"), QStringLiteral("params")})));
    tools.append(toolSchema(
        QStringLiteral("solver.update"), QStringLiteral("修改求解段帧范围、初速度或参数"),
        objectSchema({{QStringLiteral("entityUuid"), prop(QStringLiteral("string"), QString())},
                      {QStringLiteral("solverId"), prop(QStringLiteral("string"), QString())},
                      {QStringLiteral("patch"), prop(QStringLiteral("object"), QStringLiteral("startFrame/endFrame/initialVelocity/params"))}},
                     {QStringLiteral("entityUuid"), QStringLiteral("solverId"),
                      QStringLiteral("patch")})));
    tools.append(toolSchema(
        QStringLiteral("solver.delete"), QStringLiteral("删除求解段，开始帧关键帧保留"),
        objectSchema({{QStringLiteral("entityUuid"), prop(QStringLiteral("string"), QString())},
                      {QStringLiteral("solverId"), prop(QStringLiteral("string"), QString())}},
                     {QStringLiteral("entityUuid"), QStringLiteral("solverId")})));
    tools.append(toolSchema(
        QStringLiteral("solver.list"), QStringLiteral("按 startFrame 列出一条实体的求解段"),
        objectSchema({{QStringLiteral("entityUuid"), prop(QStringLiteral("string"), QString())}},
                     {QStringLiteral("entityUuid")})));
    tools.append(toolSchema(
        QStringLiteral("solver.evaluate"), QStringLiteral("内存解算求解段并返回采样点（只读）"),
        objectSchema({{QStringLiteral("entityUuid"), prop(QStringLiteral("string"), QString())},
                      {QStringLiteral("solverId"), prop(QStringLiteral("string"), QStringLiteral("省略 = 全部段"))},
                      {QStringLiteral("frames"), arrayOf(QStringLiteral("integer"), QStringLiteral("相对帧号，最多 10000 个"))}},
                     {QStringLiteral("entityUuid")})));

    tools.append(toolSchema(
        QStringLiteral("preview.render_frame"),
        QStringLiteral("渲染指定帧到临时 PNG（只读，约 2 次/秒）"),
        objectSchema({{QStringLiteral("frame"), prop(QStringLiteral("integer"), QStringLiteral("默认播放头"))},
                      {QStringLiteral("cameraUuid"), prop(QStringLiteral("string|null"), QStringLiteral("必须是摄像机实体；默认 view.state 摄像机"))},
                      {QStringLiteral("width"), prop(QStringLiteral("integer"), QStringLiteral("1..4096，只给一边时按工程比例补另一边"))},
                      {QStringLiteral("height"), prop(QStringLiteral("integer"), QStringLiteral("1..4096"))},
                      {QStringLiteral("background"), prop(QStringLiteral("string"), QStringLiteral("CSS 颜色"))},
                      {QStringLiteral("includeBase64"), prop(QStringLiteral("boolean"), QStringLiteral("返回 PNG base64"))}})));
    tools.append(toolSchema(
        QStringLiteral("preview.render_range"),
        QStringLiteral("把多帧渲染成一张网格接触表 PNG（只读，整张只算 1 次限流）；"
                       "帧选择用 frames 或 from/to/count 二选一"),
        objectSchema({{QStringLiteral("frames"), arrayOf(QStringLiteral("integer"), QStringLiteral("帧号数组，去重保序，最多 16 个；与 from/to 二选一"))},
                      {QStringLiteral("from"), prop(QStringLiteral("integer"), QStringLiteral("闭区间起始帧，0..1000000；与 frames 二选一"))},
                      {QStringLiteral("to"), prop(QStringLiteral("integer"), QStringLiteral("闭区间结束帧，>= from；必须与 from 同时提供"))},
                      {QStringLiteral("count"), prop(QStringLiteral("integer"), QStringLiteral("默认 6，1..16；均匀采样 [from,to]，含首尾"))},
                      {QStringLiteral("cols"), prop(QStringLiteral("integer"), QStringLiteral("每行列数，1..6，默认 min(帧数,4)"))},
                      {QStringLiteral("width"), prop(QStringLiteral("integer"), QStringLiteral("单格宽度 64..1024，默认 320；高度按工程宽高比"))},
                      {QStringLiteral("cameraUuid"), prop(QStringLiteral("string|null"), QStringLiteral("必须是摄像机实体；默认 view.state 摄像机"))},
                      {QStringLiteral("background"), prop(QStringLiteral("string"), QStringLiteral("CSS 颜色"))},
                      {QStringLiteral("includeBase64"), prop(QStringLiteral("boolean"), QStringLiteral("返回 PNG base64"))},
                      {QStringLiteral("showFrameLabel"), prop(QStringLiteral("boolean"), QStringLiteral("默认 true，每格左上角叠加帧号角标"))}})));
    tools.append(toolSchema(
        QStringLiteral("view.state"),
        QStringLiteral("读取 playheadFrame/selectedUuids/openTimelineOwnerUuid/view 及写权限"),
        empty));

    tools.append(toolSchema(
        QStringLiteral("asset.import"),
        QStringLiteral("把本地文件/目录复制进工程素材库（单文件/包上限 2GB），"
                       "视频自动拆 WAV；工程外路径必须由人类在 PVForge 窗口确认"),
        objectSchema({{QStringLiteral("path"), prop(QStringLiteral("string"), QStringLiteral("本地绝对路径；工程目录外会请求人类确认"))},
                      {QStringLiteral("name"), prop(QStringLiteral("string"), QString())},
                      {QStringLiteral("type"), prop(QStringLiteral("string"), QStringLiteral("image|video|audio|html"))},
                      {QStringLiteral("extractAudio"), prop(QStringLiteral("boolean"), QStringLiteral("视频默认 true"))}},
                     {QStringLiteral("path")})));
    tools.append(toolSchema(
        QStringLiteral("asset.list"),
        QStringLiteral("分页列出素材；内容文件缺失的素材仍返回，missing=true"),
        objectSchema({{QStringLiteral("type"), prop(QStringLiteral("string"), QString())},
                      {QStringLiteral("query"), prop(QStringLiteral("string"), QString())},
                      {QStringLiteral("offset"), prop(QStringLiteral("integer"), QString())},
                      {QStringLiteral("limit"), prop(QStringLiteral("integer"), QStringLiteral("1..500"))}})));
    tools.append(toolSchema(
        QStringLiteral("asset.get"),
        QStringLiteral("读取素材及引用列表；内容缺失时仍返回素材，missing=true"),
        objectSchema({{QStringLiteral("hash"), prop(QStringLiteral("string"), QString())}},
                     {QStringLiteral("hash")})));
    tools.append(toolSchema(
        QStringLiteral("asset.assign"),
        QStringLiteral("给实体换素材/内容；sourceIn 自动夹到 0..durationFrames-1"),
        objectSchema({{QStringLiteral("entityUuid"), prop(QStringLiteral("string"), QString())},
                      {QStringLiteral("assetHash"), prop(QStringLiteral("string"), QString())},
                      {QStringLiteral("sourceIn"), prop(QStringLiteral("integer"), QStringLiteral("夹到 0..max(0,durationFrames-1)"))},
                      {QStringLiteral("volume"), prop(QStringLiteral("number"), QString())}},
                     {QStringLiteral("entityUuid"), QStringLiteral("assetHash")})));
    tools.append(toolSchema(
        QStringLiteral("asset.unassign"), QStringLiteral("把实体内容清空为 empty"),
        objectSchema({{QStringLiteral("entityUuid"), prop(QStringLiteral("string"), QString())}},
                     {QStringLiteral("entityUuid")})));
    tools.append(toolSchema(
        QStringLiteral("asset.rename"), QStringLiteral("重命名素材"),
        objectSchema({{QStringLiteral("hash"), prop(QStringLiteral("string"), QString())},
                      {QStringLiteral("name"), prop(QStringLiteral("string"), QString())}},
                     {QStringLiteral("hash"), QStringLiteral("name")})));
    tools.append(toolSchema(
        QStringLiteral("asset.setTags"), QStringLiteral("设置素材标签；传空数组清除"),
        objectSchema({{QStringLiteral("hash"), prop(QStringLiteral("string"), QString())},
                      {QStringLiteral("tags"), prop(QStringLiteral("array"), QString())}},
                     {QStringLiteral("hash"), QStringLiteral("tags")})));
    tools.append(toolSchema(
        QStringLiteral("asset.delete"),
        QStringLiteral("删除素材（移入工程回收站，可撤销），必须 confirm"),
        objectSchema({{QStringLiteral("hash"), prop(QStringLiteral("string"), QString())},
                      {QStringLiteral("confirm"), prop(QStringLiteral("boolean"), QString())}},
                     {QStringLiteral("hash"), QStringLiteral("confirm")})));

    tools.append(toolSchema(
        QStringLiteral("markers.list"), QStringLiteral("按帧范围分页列出标记"),
        objectSchema({{QStringLiteral("fromFrame"), prop(QStringLiteral("integer"), QString())},
                      {QStringLiteral("toFrame"), prop(QStringLiteral("integer"), QString())},
                      {QStringLiteral("offset"), prop(QStringLiteral("integer"), QString())},
                      {QStringLiteral("limit"), prop(QStringLiteral("integer"), QString())}})));
    tools.append(toolSchema(
        QStringLiteral("markers.set"), QStringLiteral("创建或更新标记"),
        objectSchema({{QStringLiteral("id"), prop(QStringLiteral("string"), QStringLiteral("省略 = 新建"))},
                      {QStringLiteral("frame"), prop(QStringLiteral("integer"), QString())},
                      {QStringLiteral("title"), prop(QStringLiteral("string"), QString())},
                      {QStringLiteral("note"), prop(QStringLiteral("string"), QString())},
                      {QStringLiteral("tags"), arrayOf(QStringLiteral("string"), QString())},
                      {QStringLiteral("color"), prop(QStringLiteral("string"), QString())}},
                     {QStringLiteral("frame"), QStringLiteral("title")})));
    tools.append(toolSchema(
        QStringLiteral("markers.remove"), QStringLiteral("删除标记"),
        objectSchema({{QStringLiteral("id"), prop(QStringLiteral("string"), QString())}},
                     {QStringLiteral("id")})));

    tools.append(toolSchema(
        QStringLiteral("history.undo"),
        QStringLiteral("内存撤销最近的操作，不产生 git commit；git 历史面板是冷恢复"),
        objectSchema({{QStringLiteral("steps"), prop(QStringLiteral("integer"), QStringLiteral("1..10"))}})));
    tools.append(toolSchema(
        QStringLiteral("history.redo"),
        QStringLiteral("内存重做最近撤销的操作，不产生 git commit"),
        objectSchema({{QStringLiteral("steps"), prop(QStringLiteral("integer"), QStringLiteral("1..10"))}})));
    tools.append(toolSchema(
        QStringLiteral("history.log"), QStringLiteral("分页读取 git 冷备提交历史"),
        objectSchema({{QStringLiteral("offset"), prop(QStringLiteral("integer"), QString())},
                      {QStringLiteral("limit"), prop(QStringLiteral("integer"), QStringLiteral("1..100"))}})));

    tools.append(toolSchema(
        QStringLiteral("schema.get"), QStringLiteral("读取数据对象 JSON Schema 摘要"),
        objectSchema({{QStringLiteral("topic"), prop(QStringLiteral("string"), QStringLiteral("project|entity|keyframes|asset|markers|errors|solver|preview|view|all"))}})));
    tools.append(toolSchema(
        QStringLiteral("guide.get"), QStringLiteral("读取 Markdown 编写指南"),
        objectSchema({{QStringLiteral("topic"), prop(QStringLiteral("string"), QStringLiteral("authoring|workflow|solver|preview|assets|examples|pitfalls"))}})));

    QJsonObject result;
    result.insert(QStringLiteral("tools"), tools);
    return result;
}

} // namespace pvf::mcp2::detail
