# 新模型并行实现合同

> 目标：把 `docs/ENTITY_MODEL.md` 拆成可并行的底层模块。先做纯数据和纯算法，不动旧 UI/旧 MCP/旧渲染。

## 文件所有权

| Agent | 只能改 | 可读 |
|---|---|---|
| Model | `src/model/**` | 全部 |
| Solver | `src/solver/**` | 全部 |
| Assets | `src/assets/**` | 全部 |
| Gizmo | `src/gizmo/**` | 全部 |
| MCP 文档 | `docs/MCP_TOOLS.md` | 全部 |

公共类型 `src/common/Types.h` 由总控维护，agent **只读不改**。

## 硬规则

- 不改旧代码：`src/core/**`、`src/ui/**`、`src/app/**`、`src/mcp/**`、`src/render/**`、`src/html/**`、`src/media/**`、`src/main.cpp`、根 `CMakeLists.txt`。
- 不 commit、不 push。
- 编辑代码必须用 OpenCode 自带工具（`read` / `grep` / `edit` / `write` / `glob`），不要用 shell 的 `sed` / `awk` / `grep` / `cat >` 改文件。
- shell 只用于：`cmake`、编译、运行测试、创建临时目录。
- 每个模块自带 `CMakeLists.txt`，能独立构建；不要依赖根工程。
- 模块之间只通过 `src/common/Types.h` 和公开头文件交互；不要 include 旧 `core/Project.h`。
- 所有公开头文件放模块根目录，实现放 `.cpp`；测试放 `tests/`。
- 代码风格跟现有仓库一致：4 空格、`namespace pvf`、Qt6、C++20。

## 独立构建方式

```bash
cmake -S src/model -B /tmp/pvf-model-build -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/pvf-model-build -j
ctest --test-dir /tmp/pvf-model-build --output-on-failure
```

其他模块同理。Assets 可以用 FFmpeg；Model/Solver/Gizmo 只依赖 Qt6 Core/Gui。

## 公共类型

`src/common/Types.h` 已定义：

- `pvf::Uuid` = `QString`
- `pvf::AssetHash` = `QString`
- `AssetType` = Image / Video / Audio / Html
- `ContentKind` = Empty / Text / Vector / Camera / Asset
- `SolverEffect` = Gravity / Orbit / Spin / BlackHole / Spiral

## Model 模块 API

目录：`src/model/`

```cpp
namespace pvf::model {

struct EasingSpec { QString type = "linear"; double inX=0.42, inY=0, outX=0.58, outY=1; };
struct Keyframe { int frame = 0; double value = 0; EasingSpec easing; QString note; };
struct Track { QVector<Keyframe> keys; };

struct Transform {
    double x=0, y=0, rotation=0;
    double scaleX=1, scaleY=1;
    double pivotX=0, pivotY=0;
    double opacity=1;
};

struct TextContent {
    QString text, fontFamily;
    int pixelSize=96;
    QString color="#ffffff", strokeColor="#000000";
    double strokeWidth=0, lineHeight=1, letterSpacing=0, padding=6;
    double boxWidth=0, boxHeight=0;
    int align=132;
    bool bold=false, italic=false, underline=false, vertical=false, verticalRtl=true;
};

struct VectorContent {
    int shape=0;
    double width=400, height=300, cornerRadius=40;
    int sides=5;
    double innerRatio=0.45, strokeWidth=0;
    QString fillColor="#ffffff", strokeColor="#000000";
};

struct CameraContent {};

struct AssetContent {
    AssetHash hash;
    AssetType type = AssetType::Image;
    int sourceIn = 0;
    double volume = 1.0;
};

struct Content {
    ContentKind kind = ContentKind::Empty;
    TextContent text;
    VectorContent vector;
    CameraContent camera;
    AssetContent asset;
};

struct SolverSegment {
    Uuid id;
    SolverEffect effect = SolverEffect::Gravity;
    int startFrame = 0;
    int endFrame = -1;          // -1 = clip end
    int version = 1;
    QPointF initialVelocity;
    QVariantMap params;
};

struct Entity {
    Uuid uuid;
    QString name;
    Uuid parentUuid;            // empty = root
    int start=0, end=1, row=0;
    bool hidden=false, locked=false, inheritVisibility=true;
    bool visible=true, physicsInteract=false;
    Transform transform;
    Content content;
    QHash<QString, Track> tracks;
    QVector<SolverSegment> solvers;
};

struct Marker {
    QString id;
    int frame=0;
    QString title, note;
    QStringList tags;
    QString color="#ffbe3c";
};

struct ProjectSettings {
    QString name = "未命名项目";
    int width=1920, height=1080, fps=30;
    QString background="#101014";
};

struct Project {
    ProjectSettings settings;
    QHash<Uuid, Entity> entities;
    QVector<Marker> markers;
};

QJsonObject toJson(const Entity&);
std::optional<Entity> entityFromJson(const QJsonObject&, QString* error=nullptr);
QJsonObject toJson(const Project&);
std::optional<Project> projectFromJson(const QJsonObject&, QString* error=nullptr);

bool validateEntity(const Entity&, QString* error=nullptr);
bool validateProject(const Project&, QString* error=nullptr);

}
```

Model 还要提供：

- UUID 生成（UUID v4）。
- 父子查询 `childrenOf`、根实体排序（row, start, uuid）。
- 关键帧范围校验、求解段不重叠校验、父子时间范围校验。
- JSON 往返测试、非法数据测试。

## Solver 模块 API

目录：`src/solver/`

```cpp
namespace pvf::solver {

struct Pose { QPointF position; double rotation = 0; };
struct World {
    virtual ~World() = default;
    virtual Pose poseOf(const Uuid& uuid, int frame) const = 0;
    virtual QPainterPath shapeOf(const Uuid& uuid, int frame) const = 0;
    virtual bool physicsInteract(const Uuid& uuid, int frame) const = 0;
    virtual QVector<Uuid> physicsEntities(int frame) const { return {}; }
};

struct Segment {
    SolverEffect effect = SolverEffect::Gravity;
    Uuid entityUuid;        // 被驱动的运动实体：重力用自己的形状，碰撞时排除自己
    Uuid targetUuid;        // 重力=地面/方向实体；公转=中心；黑洞=中心；自转不用
    int startFrame = 0;     // relative to entity start, inclusive
    int endFrame = -1;      // relative, inclusive; -1 = clip end
    Pose startPose;
    QPointF initialVelocity; // solver space, px/second
    QVariantMap params;
};

struct Sample {
    int frame = 0;
    Pose pose;
    QPointF velocity;       // solver space, px/second
    bool collided = false;
    bool stopped = false;
};

QVector<Sample> evaluate(const Segment&, const World&, int clipDuration, int fps);

}
```

参数约定：

- `entityUuid` 必填；`targetUuid` 对 gravity/orbit/blackhole/spiral 必填，spin 忽略。
- `initialVelocity` 和 `Sample::velocity` 都在**求解空间**（与 startPose.position 同空间），不是目标局部轴。
- Gravity：方向 = `targetUuid` 实体的局部 +Y，按目标实体**当前帧 rotation**旋转；运动形状取 `entityUuid` 的形状；碰撞候选 = 所有 `physicsInteract=1` 的实体，排除 `entityUuid`，包含 target（如果 target 自己开了物理干涉）。`strength`、`elasticity`、`friction`、`stopThreshold` 参数不变。
- Orbit：中心 = `targetUuid`；椭圆经过 startPose.position；`aspect`（短轴/长轴）、`angle`（相对目标实体局部）、`direction`（+1 顺 / -1 逆）、`periodFrames`。
- Spin：`angularVelocity`（度/帧）、`direction`；轴心 = entity pivot；不需要 target。
- BlackHole：中心 = `targetUuid`；`simulationSpeed`、`mass`、`angularMomentum`、`eventHorizonRadius`、`objectRadius`、`softening`、`maxSpeed`；只改位置；可穿视界、可逃逸。
- Spiral：中心 = `targetUuid`；椭圆经过 startPose.position；`aspect`（短轴/长轴）、`angle`（相对目标实体局部）、`turns`、`direction`（+1 逆 / -1 顺）、`speedDegPerSec`；半轴线性缩到 0，末帧精确落在中心。
- 旧参数 `centerUuid` / `blackHoleUuid` / `collisionEntities` 不再作为主接口；只保留 `collisionUuids` 作为 World 枚举不可用时的回退。

要求：

- 固定步长 `1/fps`，确定性，无随机。
- 结束时速度归零、值保持。
- 单测：重力抛物线、反弹、公转经过起点、周期、顺时针/逆时针、自转、黑洞逃逸/捕获、空段/单帧段。

## Anim 模块 API

目录：`src/anim/`，依赖 `src/model` + `src/solver`。

```cpp
namespace pvf::anim {

struct Pose {
    QPointF position;
    double rotation = 0;
    double scaleX = 1;
    double scaleY = 1;
    double opacity = 1;
    bool visible = true;
};

// 统一求值：普通关键帧 + 求解段 + 父级继承 + 可见性。
Pose evaluateEntity(const model::Project&, const Uuid& entityUuid, int projectFrame,
                    const solver::World& world);

// 判断某帧是否在求解段内，供 UI/属性面板使用。
const model::SolverSegment* solverAt(const model::Entity&, int entityRelativeFrame);

}
```

要求：

- 求解段帧号相对实体 start；`projectFrame - entity.start` 得到相对帧。
- 段内：用段 start 的普通关键帧值作为初始位姿，调 `solver::evaluate`，取目标帧 sample。
- 段结束接普通关键帧：把解算结束位姿作为隐式左值，自定义插值，不能跳回段前值。
- 父级：`parentWorld × local`，用 `(x,y)+pivot+R*(S*(p-pivot))`；opacity 沿父链相乘；hidden/visible/inheritVisibility 合并。
- 求解段 World 实现：把“运动实体的相对帧”映射到项目帧，再映射到目标实体的相对帧；shapeOf 返回求解空间路径（父级变换烘焙）。
- 缓存：按实体+帧缓存最终 Pose；按实体+段+帧缓存 solver 样本；模型修改时清空对应缓存。
- 渲染、MCP preview、属性面板必须共用 `evaluateEntity`。

## Assets 模块 API

目录：`src/assets/`

```cpp
namespace pvf::assets {

QString sha256File(const QString& path, QString* error=nullptr);
QString sha256Directory(const QString& dir, QString* error=nullptr);
std::optional<AssetType> detectType(const QString& path);

struct Probe {
    QSize size;
    double durationSeconds = 0;
    int sampleRate = 0;
    int channels = 0;
};
Probe probe(const QString& path, AssetType type, QString* error=nullptr);

bool extractWav(const QString& videoPath, const QString& outPath, QString* error=nullptr);

struct PvhtmlManifest {
    QString entry = "index.html";
    QSize logicalSize{800,120};
    QVariantMap variables;
};
bool importPvhtml(const QString& sourceDirOrZip, const QString& destDir,
                  PvhtmlManifest* manifest, QString* error=nullptr);

struct StoredAsset {
    AssetHash hash;
    AssetType type = AssetType::Image;
    QString name;
    QString directory;
    QJsonObject metadata;
};

class AssetStore {
public:
    explicit AssetStore(QString projectRoot);
    std::optional<StoredAsset> importFile(const QString& path, QString* error=nullptr);
    std::optional<StoredAsset> importPvhtml(const QString& path, QString* error=nullptr);
    std::optional<StoredAsset> get(const AssetHash&) const;
    QVector<StoredAsset> list() const;
    bool remove(const AssetHash&, QString* error=nullptr);
};

}
```

要求：

- `assets/<sha256>/asset.json` + `content.<ext>`；HTML 放 `bundle/`。
- 同内容去重，保留第一次的 `name`。
- 视频导入额外拆 WAV；音频素材走同一内容寻址。
- `.pvhtml` 支持目录和 zip；校验 `index.html`。
- 单测：哈希已知内容、重复导入去重、WAV 提取、pvhtml 目录/zip、缺失素材。

## Gizmo 模块 API

目录：`src/gizmo/`

```cpp
namespace pvf::gizmo {

struct Transform {
    double x=0, y=0, rotation=0;
    double scaleX=1, scaleY=1;
    double pivotX=0, pivotY=0;
};
struct Bounds { double left=-50, right=50, top=-50, bottom=50; };

enum class Handle {
    MoveX, MoveY,
    RotateTopLeft, RotateTopRight, RotateBottomRight, RotateBottomLeft,
    ScaleTopLeft, ScaleTopRight, ScaleBottomRight, ScaleBottomLeft,
    CenterScale
};

QPointF handlePosition(Handle, const Transform&, const Bounds&, double viewScale);
std::optional<Handle> hitTest(QPointF screenPos, const Transform&, const Bounds&, double viewScale);

struct DragResult {
    Transform transform;
    bool flippedX = false;
    bool flippedY = false;
};
DragResult cornerScale(const Transform&, const Bounds&, Handle,
                       QPointF mouseLocal, bool uniform, bool allowFlip);
DragResult centerScale(const Transform&, QPointF mouseLocal, bool uniform);
DragResult rotate(const Transform&, double angleDelta, bool aroundBoundingBoxCenter);

}
```

要求：

- X/Y 轴跟随实体 rotation。
- Office 角缩放：对角固定，顶点跟鼠标，Shift 等比，越角允许负 scale。
- 中心缩放：轴心固定，X/Y 独立，Shift 等比，跨象限翻转。
- 旋转：默认 pivot，`aroundBoundingBoxCenter=true` 时绕包围盒中心。
- 单测：对角固定、翻转、中心缩放、旋转角度、旋转轴心。

## MCP 文档 Agent

只写 `docs/MCP_TOOLS.md`：

- 按 `docs/ENTITY_MODEL.md` 列出所有工具、参数、返回、错误、示例。
- 重点：`entity.createTree`、`keyframes.setMany`、`solver.create/evaluate`、`preview.render_frame`、`asset.import`、`history.undo/redo`。
- 每个参数写类型、单位、默认值、是否必填。
- 给 3 个完整 AI 工作流示例。

## 验收

每个 agent 返回：

- 改了哪些文件；
- 独立构建命令和结果；
- 测试数量/通过情况；
- 已知缺口和下一步。

总控会先合并 Model/Solver/Assets/Gizmo，再接旧 UI/MCP。
