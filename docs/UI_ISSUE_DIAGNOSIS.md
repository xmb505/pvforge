# 旧 UI 语义问题诊断

> 只读诊断，未改代码、未提交。
> 仓库：`/home/xmb505/pvforge`，分支 `new-entity-model`，诊断时 HEAD `f7ae3df`（诊断开始时 `58b16fa`；期间仅多了一笔 `USER_REQUIREMENTS.md` 文档提交）。
> 旧 UI 基线：`legacy-ui-baseline` = `ba07a59`（annotated tag `0cc6d43`）。
> 对照口径：旧代码行为以 `git show legacy-ui-baseline:<path>` 为准；新行为以当前工作树为准；冲突时以最新 `docs/USER_REQUIREMENTS.md` 和甲方本次澄清为准。
>
> **本次新增澄清（覆盖文档旧写法）**：求解段不是“停止位置预览”，而是**视口轨迹预览 + 初速度手柄**。轨迹终点必须是**实际停止 / 碰撞 / 片段末尾**；拖手柄直接得到方向/速度向量，不必手填参数；`initialVelocity` 必须写回模型。

---

## 0. 总表

| # | 用户问题 | 根因 | 优先级 |
|---|---|---|---|
| 1 | 坐标系什么时候到左上角了 | 新后端把世界原点定为画布左上角；旧 UI/旧工程以画布中心为 `(0,0)`。新建实体默认 `x=y=0`，于是出现在左上角；相机也偏移 | P0 |
| 2 | Gizmo 的 X 轴怎么放下来了 | 新 Gizmo 明确采用 Qt “Y 向下、正角顺时针”约定，Y 箭头朝下；旧 Gizmo 固定画 X 向右、Y 向上。轴又跟随实体旋转，旋转后 X 可能朝下；没有统一的 Y 翻转/旋转符号定义 | P0 |
| 3 | 关键帧曲线应该整体，不是每个参数单独算 | 旧版在“当前帧”改一次缓动，会应用到该帧**所有轨道**的 key；新版属性面板给每条轨道各放一个缓动下拉，变成逐参数独立 | P1 |
| 4 | 缓动的 UI 怎么到 clip 上了 | 旧版时间轴右键只有“打开私有时间线/删除片段”，缓动在属性面板关键帧区；新版时间轴右键和双击关键帧直接弹缓动，且命中测试只看 X 距离，点 clip 也会误开 | P1 |
| 5 | 时间轴应该从下往上，并且可以随便拖动 | 旧/新 `TimelinePanel::rowTop` 都是 `ruler + row * height`，row 0 在顶部；需求文档明确 row 0 在最底。垂直拖动又只允许落到“已有同父级 clip”的行，空白行无效 | P0 |
| 6 | 求解段为什么还要选结束帧 | UI 默认 `-1` 但把“结束帧”当参数摆出来；模型里 `-1` 只表示“解到片段末帧”，不等于物理停止/碰撞；`Sample.stopped` 又把“最后一帧”和“物理停止”混在一起 | P0 |
| 7 | 求解帧的预览没做 | 视口没有求解轨迹/速度手柄；现成的 `next/properties/SolverPreviewWidget` 只接了新壳属性面板，旧 UI 属性面板没有实例化，视口更没有 overlay | P0 |
| 8 | “我也不知道了，你看着来” | 不是缺陷；需要给默认决策和验收口径，避免继续用 UI 猜后端语义 | — |

**根因数量：7 条**（第 8 条不是缺陷，是决策/验收项；1/2 同属“世界坐标约定”，3/4 同属“缓动 UI 位置与粒度”，但修复点不同，分开列）。

---

## 1. 坐标系原点：中心 → 左上角

### 用户期望

旧版 PVForge 与 Mine-imator 的心智模型：画布中心是 `(0,0)`，新建文字/矢量/相机出现在画面中心，坐标读数围绕中心有正负。旧 UI 的视口、相机取景框、属性面板、吸附都以这个中心原点工作。

### 当前行为与证据

新后端的世界坐标是**画布左上角**：

- `src/render2/Render.cpp:1222-1242`：`canvasView()` 直接把 `worldRect = QRectF(0,0,width,height)` 映射到输出图；世界 `(0,0)` 就是输出图左上角。
- `src/render2/Render.cpp:1411-1418`：`renderFrame()` 的 `projectCanvas` 从 `(0,0)` 开始。
- `src/anim/Anim.cpp:108-121`：`composePose()` 直接 `local.translate(values.x, values.y)`，没有中心偏移，也没有 Y 翻转。
- `src/ui/Viewport.cpp:469-477`：旧壳的 session 视口 `sessionWorldToScreen()` 用 `canvas.left(), canvas.top()` 做平移；同文件的旧数据路径 `Viewport::worldToScreenTransform()`（`src/ui/Viewport.cpp:2453-2461`）仍用 `canvas.center()`。**同一个 Viewport 里两套映射并存**，session 路径切到左上，旧路径还是中心。
- `src/ui/WorkbenchPanel.cpp:1549-1557`（文字）、`1582-1601`（矢量）、`1633-1639`（相机）：新建实体只设 `start/end/row/content`，没有设 `transform`，因此 `x=y=0`。在左上原点下，新建内容以左上角为中心，半个身子在画布外。
- `src/render2/Render.cpp:1252-1262`：相机视图把相机 `pose.world` 当作取景框中心，源矩形是 `(-w/2,-h/2,w,h)`。新建相机 `x=y=0` 时，它看到的是画布左上象限，而不是整张画布。

### 旧版实现

- `legacy-ui-baseline:src/ui/Viewport.cpp:123-141`：`canvasRect()` 以控件中心为中心；`screenToCanvas()` 返回 `(pos - canvas.center()) / scale`，即世界 `(0,0)` 在画布中心。
- `legacy-ui-baseline:src/ui/Viewport.cpp:710-718`：`worldToScreenTransform()` 先 `translate(canvas.center())`，再 `scale(s,s)`。
- `legacy-ui-baseline:src/core/Project.cpp:874-890`：`makeBase()` 新建实体 `x=0, y=0`。因为旧世界原点在中心，所以新建实体出现在画面中心。
- `legacy-ui-baseline:src/core/Project.cpp:1005-1012`：`addCamera()` 也只设 `x=y=0`；旧 `Camera::state()` 的 `center` 就是取景框中心，因此新相机正好看整张画布（`legacy-ui-baseline:src/core/Camera.cpp:52-94`，`clampCenter` 以中心为基准）。

### 根因

**模型世界坐标约定从“画布中心”换成了“画布左上”，但旧 UI 的创建默认值、视口映射、相机语义、MCP/缓存坐标都没有做转换。** 这不是单个 `Viewport` 的偏移 bug，而是 `render2` + `anim` + `Viewport` + `WorkbenchPanel` + `cameraView` 共用的一套坐标基准变了。

### 修复方案

推荐 **方案 A：把后端世界坐标改回“画布中心”**，因为甲方明确要求旧 UI 逻辑不动、后端适配 UI：

- `src/render2/Render.cpp`
  - `canvasView(output, settings)`：`worldRect = QRectF(-w/2, -h/2, w, h)`。
  - `renderFrame()` 的 `projectCanvas` 同样改为中心矩形；扩展缓存 `worldRect` 也以中心为基准。
  - `cameraView()` 已经用 `(-w/2,-h/2)` 的源矩形，语义正好匹配中心原点；只需保证相机 `x/y` 仍是取景框中心。
- `src/ui/Viewport.cpp`
  - `sessionWorldToScreen()` 改为 `translate(canvas.center())`。
  - `sessionSceneRect()`、`sessionScreenToProject()`、`sessionCameraPolygon()`、`hitTestSession()` 全部随中心矩形调整。
- `src/anim/Anim.cpp`：`composePose()` 的 `x/y` 语义不变（仍是中心原点下的世界/父级坐标），不需要加偏移；只要渲染和视口使用同一中心矩形。
- `WorkbenchPanel`：新建实体可继续 `x=y=0`，因为 0 就是中心；相机也继续 0。
- `src/next/app/WorkbenchServices` / 工作台缓存：缓存元信息里的世界矩形、扩展百分比、相机视图必须一起改为中心矩形，否则缓存命中和画面会错位。
- `src/mcp2` / `export2`：都走 `render2`，会随 `canvasView` 一起正确；但 MCP 文档和测试里“世界左上角”的断言要改。
- 旧工程不迁移，因此不需要兼容旧文件里的坐标；这条只影响新工程和当前测试。

**改动面：** `render2` / `anim` / `Viewport` / `WorkbenchServices` / MCP 测试必须一起改；`model::Transform` 字段不用变，但世界原点语义变。

**风险：高。** 影响渲染、视口、相机、工作台缓存、MCP 预览、导出和全部渲染测试。必须用“同一帧、同一相机、视口截图 vs `preview.render_frame`”对拍。

**备选方案 B（不推荐）**：保留模型左上原点，只在旧 UI 层做 `+ (w/2,h/2)` 偏移和 Y 翻转。改动小，但 MCP/缓存/导出仍是左上坐标，AI 读到的坐标和用户在属性面板看到的坐标会永久分裂；违反“后端适配 UI 逻辑”。

**优先级：P0。**

---

## 2. Gizmo 轴向与旋转符号

### 用户期望

- 选中实体后，X 箭头向右、Y 箭头向上；拖动 X 只横移、拖动 Y 只纵移。
- 需求又明确“X/Y 轴跟随实体旋转”（`docs/ENTITY_MODEL.md:253`，决策账本 #60），所以实体旋转后箭头应跟着转。
- 旋转拖动方向要符合视觉直觉：顺时针拖，画面顺时针转。

### 当前行为与证据

- `src/gizmo/Gizmo.h:29-31` 把约定写死为：“Rotation is in degrees with Qt's convention: a positive angle maps local +X towards local +Y. Qt's Y axis grows downwards, so on screen that looks clockwise.”
- `src/gizmo/Gizmo.cpp:187-190`：`MoveX = pivot + rotate((len,0), rotation)`，`MoveY = pivot + rotate((0,len), rotation)`。在 `rotation=0` 时，X 向右、**Y 向下**。
- `src/ui/Viewport.cpp:1432-1437`：session Gizmo 直接画 `pivot → MoveX`（红）和 `pivot → MoveY`（绿），没有 Y 翻转。
- `src/ui/Viewport.cpp:705-716`：Gizmo 的 rotation 取当前帧轨道值；所以有旋转动画时，箭头会跟着转。`rotation=90` 时 `MoveX` 朝下，这正是“X 轴怎么放下来了”的可见来源之一。
- `src/ui/Viewport.cpp:740-751`：solver 驱动实体从 `pose.world` 反推 rotation，用 `atan2(local.m12()/scaleX, local.m11()/scaleX)`；这套符号与 `Gizmo.h` 的 Y-down 约定绑定。

### 旧版实现

- `legacy-ui-baseline:src/ui/Viewport.cpp:805-819`：旧 Gizmo **固定**画 X 向右、Y 向上；`drawGizmo()` 没有把 rotation 应用到轴向量。
- `legacy-ui-baseline:src/ui/Viewport.cpp:1580-1584`：旧版拖 Y 轴时 `y = startY + (mouseY - startMouseY)/s`，即屏幕向下拖，`y` 增大。
- 因此旧版本身存在一个矛盾：**Gizmo 的 Y 箭头画向上，但数据正方向是屏幕向下**。用户记忆中的“Mine-imator 坐标”很可能是 Y 向上；旧代码只是数据沿用 Qt 的 Y 向下。
- 旧版渲染：`legacy-ui-baseline:src/render/SceneRenderer.cpp:28-44` 用 `QTransform::translate(x,y) + rotate(rotation) + scale`，没有 Y 翻转；正 rotation 在屏幕上顺时针。

### 根因

**没有定义“世界 Y 正方向”和“Gizmo 轴方向”的唯一约定，也没有在 Gizmo/渲染/拖动/求解器之间做符号转换。** 具体是两层：

1. 新 Gizmo 采用 Y 向下，旧 Gizmo 画 Y 向上；用户看到 Y 箭头朝下。
2. 新 Gizmo 的轴跟随 `rotation`（这是需求 #60），但旋转符号仍按 Qt 顺时针；如果用户按 Mine-imator 的 Y 向上/逆时针理解，旋转后 X 就会“跑到下面”。

代码中没有发现 X/Y 交换。现场判定：

- `rotation=0` 时 X 朝下：不是 Gizmo 轴定义，查 `sessionParentToWidget()` 的父级矩阵/负 scale，或 `sessionGizmoTransform()` 读到的 `rotation` 是否已经是 90。
- `rotation=90` 时 X 朝下：这是当前 Y-down + 顺时针约定的必然结果，属于坐标约定问题，按上面的 A/B 方案统一改。

### 修复方案

先拍板坐标约定，再一次性改：

- **约定 A（推荐，Mine-imator 语义）**：世界原点中心、Y 向上、正 rotation 逆时针（或保持“视觉顺时针”但内部取负）。**只在一个边界做 Y 翻转**：要么 `render2` / `Viewport` 输出时翻，要么 `anim` 生成矩阵时翻；不要 `gizmo`、`anim`、`render2` 各翻一次，否则会双重镜像。Gizmo 的 `handlePosition`、`hitTest`、`rotate`、`cornerScale` 必须在同一个模型坐标约定下工作，不能一半模型坐标一半屏幕坐标。
- **约定 B（保持旧数据）**：世界原点中心、Y 向下、正 rotation 顺时针；Gizmo 的 Y 箭头仍画向上，但拖动 Y 向下时内部取负。这样数据与旧版一致，但“Y 向上”只是显示约定，MCP/属性面板读数仍是 Y 向下，容易再次混淆。

具体文件：

- `src/gizmo/Gizmo.h/.cpp`：删除“Qt Y grows downwards”作为唯一约定；新增 `worldToScreen` / `screenToWorld` 的 Y 翻转和旋转符号函数；`handlePosition(MoveY)` 的输出方向按约定修正。
- `src/anim/Anim.cpp`：`composePose()` 的 rotation 符号与 Gizmo 保持一致；父子链、负 scale 一起验证。
- `src/render2/Render.cpp`：`canvasView` 和 `cameraView` 共用同一 Y 约定；不能在渲染里翻、Gizmo 里不翻。
- `src/ui/Viewport.cpp`：`sessionWorldToScreen()`、`applySessionGizmoDrag()` 的 `MoveX/MoveY`、`Rotate` 角度、`sessionGizmoTransform()` 的 atan2 全部走同一转换。
- 求解器 `src/solver/*`：重力方向文档写“目标局部 +Y 向下”（`docs/ENTITY_MODEL.md:468`）。如果世界改 Y 向上，重力加速度要改为 `-Y`，否则物体会往天上掉。

**风险：高。** 任何一处漏翻都会造成“Gizmo 和画面差一个镜像”。必须加测试：rotation = 0/90/180/-90 时 X/Y 箭头屏幕方向、拖 X/Y 的数值符号、顺时针旋转拖动的角度符号、父级旋转/负 scale 下的轴向。

**优先级：P0。**

---

## 3. “整体曲线”：旧版到底指什么

### 先给结论

旧版**不是**“所有轨道在整个时间轴上共用一条全局缓动曲线”。旧版的“整体”有两层，代码都能证明：

1. **同一关键帧时间点，所有轨道的 key 共用一次缓动选择。** 在属性面板点“缓动”，旧版会收集该实体**所有 track 中 frame 相同的 key**，一次性写入同一个 `EasingSpec`。
2. **曲线编辑器把多条属性曲线画在同一张图里**（X/Y/旋转/缩放 X/缩放 Y/透明度），不是每个参数单独一个图。

用户说的“关键帧曲线应该是整体的，而不是每个参数单独算”，对应第 1 层；如果用户指的是图，则对应第 2 层。

### 旧版证据

- `legacy-ui-baseline:src/ui/PropertiesPanel.cpp:180-211`：缓动按钮从任意 track 中找 `key.frame == m_frame` 的 key 作为当前值；缓动选择器标题是“进入本帧：%1s → %2s”，说明它按“到达该帧的进入段”工作。
- `legacy-ui-baseline:src/app/MainWindow.cpp:3060-3088`：`onKeyframeEasingEdited(id, frame, spec)` 遍历 `entity->tracks` 的**所有** track，把该 frame 的每个 key 都加入 `KeyframeEasingRecord`，然后逐条 `setKeyframeEasing`。这就是“整体”的硬证据。
- `legacy-ui-baseline:src/ui/CurveEditor.cpp:34-44`：`kCurves[]` 固定列出 `x/y/rotation/scaleX/scaleY/opacity` 六条曲线；`CurveEditor::paintEvent` 在同一张图里画多条 `Track`。这是“曲线图整体显示”的证据。
- 旧版交付壳里 `CurveEditor` 已被 `hide()`：`legacy-ui-baseline:src/app/MainWindow.cpp:343-348`、`legacy-ui-baseline:src/app/MainWindow.cpp:465-470`；当前壳同样隐藏（`src/app/MainWindow.cpp:359-363`、`480-482`）。所以如果用户说的是“曲线图”，它在旧壳里本来就是隐藏/半成品；如果用户说的是“缓动选择”，那才是旧 UI 真正在用的交互。

### 当前行为与证据

- `src/ui/PropertiesPanel.cpp:3303-3335`：`rebuildTrackRows()` 给每条轨道创建一个 `QComboBox`（`trackEasing_<track>`），`currentIndexChanged` 时只调用 `sessionSetEasing(track, spec)`，只改这一条轨道。
- `src/ui/PropertiesPanel.cpp:3007-3026`：`sessionSetEasing()` 只构造 `KeyRef{m_sessionUuid, track, relative}`，`track` 非空，所以只改单轨。
- `src/ui/PropertiesPanel.cpp:1174-1175`：旧版的单个 `m_easeButton` 在 session 模式下被 `hide()`，注释直接写“replaced by the per-track combo”。
- `src/app/MainWindow.cpp:5037-5068`：`onKeyframeEasingEdited()` 仍走旧 `core::Project`/`History`，不认 session 模型；所以 session 模式即使恢复旧按钮，也不能把信号接到这个槽，必须走 `EditActions::setKeyframeEasing`。
- `src/ui/TimelinePanel.cpp:1608-1655`：时间轴的 `openEasingForSelection()` 如果选中的是**合并菱形**（`selection.track` 为空），会调用 `setKeyframeEasing(refs, ...)`；`EditActions::planSetKeyframeEasing` 在 `ref.track.isEmpty()` 时确实会遍历所有 track（`src/app/EditActions.cpp:1104-1115`）。**后端已经支持“整帧所有轨道”，是属性面板的逐轨下拉把它拆散了。**

### 根因

**旧 UI 的“按帧整体缓动”被替换成了“按轨道逐参数缓动”。** 后端 API `EditActions::setKeyframeEasing` 支持空 track = 全部轨道，但旧属性面板没有恢复旧按钮，反而隐藏它并新增逐轨 combo。

### 修复方案

- `src/ui/PropertiesPanel.cpp`
  - 在 `buildSessionUi()` 里恢复 `m_easeButton`（不要 `hide()`）；同时删除 `buildUi()` 里 `connect(m_easeButton, ...)` 开头的 `if (m_sessionMode) return;`，否则按钮显示了也不会工作。
  - 按钮文本按当前帧显示“缓动：<预设> / 首帧无进入段”；点击时收集 `KeyRef{m_sessionUuid, QString(), relative}`（空 track），调用 `EditActions::setKeyframeEasing`；当前值从任意有 key 的轨道读取，和旧版一致。
  - 删除/隐藏 `trackEasing_<track>` 这一列；至少不能让它是唯一入口。
- `src/ui/TimelinePanel.cpp`
  - 合并菱形选择（空 track）继续走整帧批量；这是正确路径。
- 如果“整体曲线”指图：
  - 旧 `CurveEditor` 依赖 `core::Project`/`core::Track`，不能直接在新模型上用；需要移植到 `model::Entity::tracks` + `anim::evaluateTrack`，或者暂时保持隐藏并在文档里明确“v1 不提供曲线图”。这是一条独立工作，不要和缓动选择混在一起。
- 验收：同一帧给 x/y/rotation 都打 key，改一次缓动，三条轨道的 JSON `easing` 必须相同；`Ctrl+Z` 一次撤回三条。

**改动面：** 只改 `src/ui/PropertiesPanel.cpp`（以及可选的 `CurveEditor` 移植）；`model` / `anim` 不改，后端批量 API 已存在。

**风险：低到中。** 后端已支持批量；主要是 UI 回退和测试。曲线图移植风险中。

**优先级：P1。**（如果甲方把“整体曲线图”视为 v1 必须，则升 P0。）

---

## 4. 缓动 UI 为什么跑到 clip 上

### 用户期望

旧版：时间轴右键 clip 只有“打开私有时间线 / 删除片段”；缓动在右侧属性面板的“关键帧”区，针对当前帧的关键帧组。时间轴双击 clip 是打 key，不是弹缓动。

### 当前行为与证据

- `src/ui/TimelinePanel.cpp:3538-3562`：右键时先 `hitKeyframeRow(row, hitX)`；只要命中，就弹出“编辑缓动… / 复制关键帧 / 删除关键帧”。
- `src/ui/TimelinePanel.cpp:3246-3259`：双击时如果 `hitKeyframeRow` 命中，直接 `openEasingForSelection()`，不经过属性面板。
- `src/ui/TimelinePanel.cpp:4098-4114`：`hitKeyframeRow()` 只比较 `abs(x - xFromFrame(frame)) <= 10`，**完全不检查 y 是否落在关键帧菱形上**。因此只要 clip 内某个 key 的 X 坐标离鼠标 10px 以内，右键 clip 也会被当成“点在关键帧上”，弹出缓动菜单。这就是“缓动 UI 到 clip 上”的直接触发原因。
- `src/ui/TimelinePanel.cpp:3552`：菜单文案就是“编辑缓动…”。

### 旧版实现

- `legacy-ui-baseline:src/ui/TimelinePanel.cpp:2130-2154`：`contextMenuEvent` 只检查 clip 矩形，菜单只有“打开私有时间线 / 删除片段”，没有任何缓动入口。
- `legacy-ui-baseline:src/ui/TimelinePanel.cpp:1929-1957`：双击 clip 只是 `keyframeAddRequested` + 选中该帧，不弹缓动。
- `legacy-ui-baseline:src/ui/PropertiesPanel.cpp:173-212`：缓动按钮在属性面板“关键帧”区。

### 根因

**时间轴新增了旧版没有的缓动入口；并且关键帧命中测试只按 X 距离，导致 clip 右键误判为关键帧右键。** 两个问题叠加，用户看到的就是“缓动跑到 clip 上”。

### 修复方案

- `src/ui/TimelinePanel.cpp`
  - 删除 `contextMenuEvent` 里的“编辑缓动…”项；时间轴右键恢复旧行为（打开私有时间线/删除/切分/换父级，按新模型保留）。
  - 删除 `mouseDoubleClickEvent` 里 `openEasingForSelection()` 的调用；双击 key 只选中/打 key。
  - 如果保留“关键帧右键菜单”，必须把 `hitKeyframeRow` 改为命中真实菱形区域：X 在菱形半宽内，Y 在该行关键帧绘制带内；不能只用 X 距离。
- `src/ui/PropertiesPanel.cpp`
  - 恢复旧 `m_easeButton`（见第 3 条），作为唯一缓动入口；按钮位于“关键帧”区，作用于当前帧整组。
- 验收：右键 clip 空白处只出现片段菜单；右键关键帧菱形只选中/操作关键帧；缓动只在属性面板出现。

**风险：低。** 纯 UI 接线和命中测试。

**优先级：P1。**

---

## 5. 时间轴应从下往上，且能随便拖

### 用户期望

- PR 式：row 0 在最底，row 越大越靠上；新实体从底部往上加，不被上面的行压住。
- clip 可以拖到任意行（包括空白行），上下拖动不受“必须落在已有 clip 上”的限制；时间方向无限长。

### 当前行为与证据

- `src/ui/TimelinePanel.cpp:792-803`：`rowTop(row) = kRulerHeight + row * kRowHeight - m_scrollY`；row 0 在顶部，row 增大向下。
- `src/ui/TimelinePanel.cpp:757-783`：`rowIds()` 直接以模型 row 为下标，`row 0` 是数组第 0 项；绘制循环 `2515-2530` 从 row 0 往下画。
- `src/app/TimelineAdapter.cpp:171`：`data["timelineRow"] = source.row`，没有做“视觉行 = 最大行 - 模型行”的转换。
- `docs/ENTITY_MODEL.md:246`、`:275-281`、`docs/STATUS.md:38` 明确要求 row 0 在最底、新行向上加。
- 垂直拖动：`src/ui/TimelinePanel.cpp:1869-1919` 松手时 `sessionBaseEntityAtRow(baseRow)` 找目标；只有目标 clip 存在且同父级时才 `siblingOrderForDrop`。拖到空白行时 `targetId` 为空，`order` 为空，`planMove` 不执行，clip 回到原行。
- `src/ui/TimelinePanel.cpp:1222-1234`：`sessionBaseEntityAtRow()` 只返回“该行已有实体”的 id；没有“把实体放到第 N 行”的 plan。
- 水平方向本身没有固定右边界：`EditActions::planMove()` 只校验 `start >= 父级 start/0`（`src/app/EditActions.cpp:325-346`），右移不限；所以“不能随便拖”的主因是垂直行，不是时间轴长度。

### 旧版实现

- 旧版基线也是 top-down：`legacy-ui-baseline:src/ui/TimelinePanel.cpp:616-627` 的 `rowTop` / `rowAt` 与新代码同构。也就是说，**旧 UI 代码本身是 top-down，需求文档是在旧 UI 之后提出 bottom-up 的**；这次集成没有把需求落进去。
- 旧版垂直拖动：`legacy-ui-baseline:src/ui/TimelinePanel.cpp:2114-2122` 有 `moveEntityToRow`，可把实体放到目标行（并处理占用者交换），比新 session 路径更自由；但方向仍是 top-down。

### 根因

**直接复用了旧 `TimelinePanel` 的 top-down 几何，没有按新需求翻转 row 映射；同时 session 拖动只实现了“拖到另一个 clip 上换顺序”，没有实现“拖到任意行”。** 参考实现 `src/next/timeline/TimelineWidget.cpp:488-492` 已经是 bottom-up：

```text
rowTop(row) = body.bottom() - (row + 1) * kRowHeight + 1 + m_rowScroll
rowAt(y)    = (body.bottom() - y + m_rowScroll) / kRowHeight
```

### 修复方案

- `src/ui/TimelinePanel.cpp`
  - 采用 `next/timeline` 的 bottom-up 公式：`rowTop`、`rowAt`、`clipRect`、`sessionBaseClipRect`、`hitKeyframeRow`、`dropRow` 全部改。
  - `rowIds()` 保持“row 下标”不变，只改 Y 映射；不要改模型 row 语义。
  - 垂直滚动条：`maxY = rows * kRowHeight - bodyHeight`，滚动值语义从“顶部偏移”改为“底部锚定偏移”；`autoScrollTick()` 的上下方向也要翻。
  - 拖到空白行：新增 `EditActions::planMoveToRow(uuid, row)`（或在 `planMove` 增加 `targetRow` 参数），松手时无论目标行是否有 clip 都提交；有目标 clip 时才走换顺序/换父级询问。
  - 拖动预览：`updateSessionDragPreview()` 的 MoveClip 分支要把目标行传给 plan，不能只在松手时算。
- `src/next/timeline/TimelineWidget.cpp` 只作参考，不直接替换旧时间轴。
- 验收：空工程 row 0 在最底；新建三个实体后从下往上排列；把中间 clip 拖到最上方空白行，松手后 row 改为最大行+1；拖到最下方空白行 row 改为 0；滚动/私有时间线/关键帧命中仍正确。

**改动面：** 只改 `src/ui/TimelinePanel.cpp` / `TimelineAdapter`；`model::Entity::row` 语义不改，`render` / `anim` 不动。

**风险：中高。** 所有 Y 相关命中、绘制、滚动、拖拽、弹出浮层几何都要一起改；建议先加 `rowTop/rowAt` 单元测试，再改交互。

**优先级：P0。**

---

## 6. 求解段结束帧：不该强制人选

### 用户期望（本次澄清）

- 用户不应该必须先知道物体在哪里停下，再手填“结束帧”。
- 结束帧应由**物理停止 / 碰撞 / 片段末尾**自动决定；显式结束帧只作为高级覆盖。
- 轨迹终点就是实际停止/碰撞/片段末尾。

### 当前行为与证据

- `src/model/Entity.h:18-27`：`SolverSegment.endFrame = -1; // -1 = solve until the last frame of the clip`。`-1` 的语义是“片段末帧”，**不是“物理停止”**。
- `src/anim/Anim.cpp:17-23`：`effectiveEnd()` 对 `endFrame < 0` 返回 `entity.duration()-1`。
- `src/solver/Solver.cpp:22-28`：同样把 `-1` 解释为 `clipDuration-1`，然后按固定步长一直算到该帧。
- `src/solver/GravitySolver.cpp:345-357`：`last` 时强制 `velocity=0; stopped=true`；最后一帧的 `stopped` 与“物理撞停”混在一起。碰撞时也会 `stopped=true`（`340-341`），但没有单独字段区分“碰撞停”“阈值停”“片段末尾”。
- `src/ui/PropertiesPanel.cpp:1341-1347`：新建求解段 UI 有“结束帧” spin，默认 `-1`、特殊值文本“片段末帧”；`3790-3798` 仍然读取这个值并校验。也就是说默认虽然不是强制，但 UI 把“结束帧”呈现成一个必须理解的参数。
- `src/ui/PropertiesPanel.cpp:3524-3529`：已有求解段卡片同样有“结束帧” spin。
- `docs/ENTITY_MODEL.md:424-428`：旧设计写“结束帧只是停止标记”“没有结束帧时解到 clip 最后一帧”；没有“物理停止自动结束”的语义。
- `docs/USER_REQUIREMENTS.md:56`（本次新增）：轨迹显示到**实际停止/碰撞/片段末尾**；这条新要求覆盖旧文档的“-1 = 片段末帧”。

### 旧版实现

旧 UI 交付路径（`legacy-ui-baseline` 的 `src/app`、`src/ui`）没有任何 solver 调用（`grep -i solver` 命中 0 次）；`src/solver` 虽然在基线 tag 里，但属于新后端模块，旧壳不实例化。旧 UI 没有“求解段结束帧”概念，因此这里不是“旧版怎么做”，而是“旧 UI 不该多出一个旧版没有的必填参数”。用户的心智来自旧版普通关键帧：物体停在哪，画面就看到哪，不需要预先填一个停止帧。

### 根因

**模型把 `endFrame` 定义成“解算范围终点”，UI 把它呈现成“用户要选的结束帧”；而求解器内部的物理停止没有成为模型语义。** 具体有三层：

1. `-1` 只等于 clip 末帧，不检查 `Sample.stopped/collided`。
2. `Sample.stopped` 在最后一帧被强制置真，无法区分“物理停止”和“范围结束”。
3. 创建界面把结束帧作为普通参数，默认值虽然是 -1，但用户仍需理解它。

### 修复方案

**模型/求解器：**

- `src/model/Entity.h`：把 `endFrame` 语义改为：
  - `-1` = 自动：解算到第一次物理停止/碰撞；没有停止则到片段末帧。
  - `>=0` = 显式覆盖（高级）。
  - 文件格式不变（仍是 int），但注释、校验、MCP schema 文档要改。
- `src/solver/Solver.h/.cpp`：
  - 增加 `struct Result { QVector<Sample> samples; int stopFrame; StopReason reason; }` 或给 `Sample` 增加 `bool physicallyStopped` / `StopReason`；不要让“最后一帧”污染 `stopped`。
  - `evaluate()` 在自动模式下可以在第一次 `physicallyStopped` 后提前结束采样；也可以继续采但返回 `stopFrame` 给 UI/时间轴用。推荐返回 `stopFrame`，渲染仍可保持“值保持”。
- `src/anim/Anim.cpp`：`effectiveEnd()` 使用 `Result.stopFrame`；段后普通 key 的锚点用停止帧位姿，不是 clip 末帧位姿。
- `src/app/EditActions.cpp`：求解段重叠校验用“计算出的有效结束帧”，不是单纯 `endFrame`；裁剪 end 切进段时把显式结束标记冻结在新末帧（保留现有规则）。
- `src/mcp2/SolverTools.cpp`：`endFrame` 缺省/`-1` 文档改成“自动物理停止/片段末尾”，不要写“片段末帧”。

**UI：**

- `src/ui/PropertiesPanel.cpp`
  - 新建求解段：删除“结束帧”spin 的默认可见性；改成只读标签“结束：自动（碰撞/停止阈值/片段末尾）”，加一个“手动指定结束帧”高级复选框，勾选后才显示 spin。
  - 已有求解段卡片：同样只读显示自动算出的 `stopFrame + 原因`；高级覆盖才显示可编辑 spin。
  - `createSolverFromPlayhead()` 默认 `segment.endFrame=-1`，不再要求用户填。
- 时间轴：求解段色带的结束位置用 `Result.stopFrame`，不是 clip 末帧；段后空白表示“已停止/保持”。
- 验收：重力段从高处落下，撞到 `physicsInteract=1` 的地面后自动停；UI 显示“第 N 帧碰撞停止”；不填结束帧也能创建；轨迹终点就是停止点；把 clip 拉长不会让段复活（沿用裁剪规则）。

**风险：高。** 触及模型语义、求解器、动画求值、时间轴、MCP、测试。好消息是 `endFrame=-1` 已经是默认值，文件格式兼容；行为变化主要是“更早停止”，符合用户澄清。

**优先级：P0。**

---

## 7. 求解预览：视口轨迹 + 初速度手柄

### 用户期望（本次澄清，不是“停止位置预览”）

- 视口里显示求解轨迹（折线/曲线），从开始位姿到实际停止/碰撞/片段末尾。
- 轨迹起点/实体上有一个**初速度手柄**；拖动它直接得到方向/速度向量，不需要手调“初速度 X/Y”spin。
- 参数变化（重力、初速度、目标、碰撞体）时轨迹实时更新。
- 松手提交一次，`initialVelocity` 写回模型；撤销一次回到拖动前。

### 当前行为与证据

- 视口 session 绘制只有相机 overlay、选中 Gizmo、参考线：`src/ui/Viewport.cpp:1821-1865` 的 `paintSession()` 调用 `drawSessionCameraOverlay()`、`drawSessionSelection()`；`drawSessionSelection()` 只调用 `drawSessionGizmo()`（`668-685`）。没有求解轨迹/速度手柄。
- `src/ui/Viewport.cpp` 里与 solver 有关的只有 `solverDrivesProperty()`（`204-216`）和 `sessionGizmoTransform()` 对 solver 位姿的反推（`718-758`）；没有任何 trajectory/velocity 绘制或命中。
- 旧 UI 属性面板只放了“初速度 X/Y”两个 `QDoubleSpinBox`：`src/ui/PropertiesPanel.cpp:3531-3548`；没有轨迹预览控件。
- 新壳属性面板已经有现成的 `SolverPreviewWidget`：`src/next/properties/PropertiesPanel.cpp:2342-2345` 实例化，`src/next/properties/SolverPreviewWidget.h:15-25` 明确写了“trajectory + draggable initial-velocity handle + commit on release”。
- 旧 UI 的属性面板没有 include/实例化 `SolverPreviewWidget`（`grep` 无命中），所以旧壳完全缺失。

### 旧版实现

旧版没有求解器，因此没有“旧版轨迹预览”可直接抄；但旧版有两条可复用的交互原则：

- 拖拽实时预览、松手一次提交（旧 Gizmo/缩放/关键帧拖动都是这个模式）。
- 数值面板不是唯一入口；视口手柄优先（旧 Gizmo 的 X/Y、旋转、pivot 都是直接拖）。

### 根因

**新后端的 `SolverPreviewWidget` 已经实现了轨迹和初速度手柄，但只接到 `src/next` 新壳属性面板；旧 UI 集成时没有把它接进旧 `PropertiesPanel`，更没有在旧 `Viewport` 里做 overlay。** 这不是“没实现”，而是“实现放在不交付的壳里，旧壳没接线”。

### 修复方案

**A. 视口 overlay（本次澄清的重点）**

在 `src/ui/Viewport.cpp` 增加：

- `drawSessionSolverOverlay(QPainter&)`：在 `paintSession()` 的 `drawSessionCameraOverlay()` 之后、`drawSessionSelection()` 之前调用（`1850` 附近）。
- 取当前选中实体 + 当前播放头所在的求解段（或属性面板选中的 solver id）：
  - 用 `render2::SceneWorld` + `solver::evaluate()` 取 `samples`；`SolverPreviewWidget` 内部的 `PreviewWorld` adapter（`src/next/properties/SolverPreviewWidget.cpp:337-430`）可以提取到公共 helper，供视口和属性面板共用。注意 `anim::EvalContext` 是 `Anim.cpp` 私有实现，不是公开 API，视口不要直接 include 它。
  - 轨迹点从 solver/parent 空间映射到 widget：`sessionParentToWidget(entity).map(point)`；不要直接拿世界坐标，父级变换会错。
  - 画折线、起点圆点、停止/碰撞点（不同颜色/图标）、目标实体连线、当前帧游标；轨迹裁剪到画布/扩展矩形。
- 初速度手柄：
  - 只在 gravity/blackhole 显示（`SolverPreviewWidget::usesVelocity()`，`src/next/properties/SolverPreviewWidget.cpp:331-335`）；orbit/spin 不显示。
  - 箭头长度用 `kVelocitySeconds = 0.25`（`src/next/properties/SolverPreviewWidget.cpp:22-24`），tip = `startPose + initialVelocity * 0.25`，与属性面板预览保持一致。
  - 命中半径 13px（同文件 `kArrowHitRadius`，`28`），拖拽时把 widget 坐标逆变换回 solver/parent 空间，`velocity = (solverPos - startPose) / 0.25`，clamp 到 ±1e6。
  - 拖动中直接改 `m_session->mutableProject()` 的 `SolverSegment.initialVelocity`，实时重算轨迹；松手调用 `Document::commitEntity(uuid, "pvf: set solver initial velocity")` 一次；失败恢复拖动前值。
  - 这段逻辑可以复用 `SolverPreviewWidget::mouseMoveEvent/mouseReleaseEvent`（`src/next/properties/SolverPreviewWidget.cpp:819-930`），不要重写数学。

**B. 属性面板（次级，但顺手补上）**

- `src/ui/PropertiesPanel.cpp`：在求解段卡片里嵌入 `next::SolverPreviewWidget`（构造函数接受 `next::Document*`，`ProjectDocument` 就是；`src/next/properties/PropertiesPanel.cpp:2342` 是现成用法），`setSolver(entityUuid, solver.id)`，文档变化时 `refreshPreview()`。
- 构建接线：当前 `pvforge` 目标直接编译 `src/ui/PropertiesPanel.cpp` 和 `src/next/properties/SpecialContentEditor.cpp`（`CMakeLists.txt:91-95`），但没有链接 `pvf_next_properties`；需要把 `src/next/properties/SolverPreviewWidget.cpp/.h` 加入 `pvforge` 源列表，或 `target_link_libraries(pvforge PRIVATE pvf_next_properties)`（`CMakeLists.txt:133-136`、`:170`）。
- 保留“初速度 X/Y”spin 作为精确输入；手柄和 spin 双向同步。
- 如果不想在旧 UI 依赖 `next` 命名空间，可把 `SolverPreviewWidget` 迁到 `src/ui/` 或 `src/app/`，但不要复制第二份数学。

**C. 模型写回**

- 新增 `EditActions::setSolverInitialVelocity(uuid, solverId, QPointF, QString*)`，内部 `updateEntity` + 校验 solver 存在/未被锁定；或直接复用 `Document::commitEntity`。
- 写入只改 `initialVelocity`，不动被驱动轨道、不动关键帧、不触发 solver 段内 key 校验。
- 一次拖动一个 commit；拖动过程不 commit，避免 undo 栈被每帧塞满。

**改动面：** `src/ui/Viewport.cpp`、`src/ui/PropertiesPanel.cpp`、`src/app/EditActions.*`；`model::SolverSegment.initialVelocity` 字段已存在，不改文件格式；`solver` 的求值可复用，不改步长。

**风险：中高。** 主要风险是坐标空间（solver 是 parent space，不是 world）和 Y 约定；必须用“轨迹终点 = 停止点”“拖手柄后 JSON 的 initialVelocity 与预期一致”“撤销一次回到拖动前”三条验收。

**优先级：P0。** 这是用户本次明确要求的功能，不是可选优化。

---

## 8. “我也不知道了，你看着来”：建议的默认决策

这不是缺陷；建议按下面的默认值推进，甲方只需要在三个点拍板（见第 9 节）。

### 8.1 默认决策

1. **坐标**：采用“画布中心原点 + Y 向上”的 Mine-imator 语义；后端 `render2/anim/gizmo/solver` 统一适配，旧 UI 布局和面板不动。
2. **时间轴**：严格按 `docs/ENTITY_MODEL.md` 的 bottom-up；row 0 最底；drag 到任意行都算数。
3. **缓动**：按旧版“当前帧整组缓动”，不按轨道；属性面板恢复旧按钮；时间轴移除缓动入口。
4. **求解结束**：默认自动（物理停止/碰撞/片段末尾）；高级才显示手动结束帧。
5. **求解预览**：视口轨迹 + 初速度手柄为一等功能；属性面板的 `SolverPreviewWidget` 作为精确输入补充。
6. **曲线图**：v1 先不恢复旧 `CurveEditor`（旧壳里本来就 hidden）；如果甲方要“整体曲线图”，另开一项移植，不和缓动选择混在一起。

### 8.2 验收场景（建议直接写进测试）

- 新建 1920×1080 工程，创建文字：出现在画布中心；属性面板 X/Y=0；相机新建后看整张画布。
- 选中实体 rotation=0：X 箭头向右、Y 箭头向上；拖 X 只改 x、拖 Y 只改 y；旋转 90° 后箭头跟随且拖动方向与视觉一致。
- 时间轴空工程 row 0 在最底；连续创建三个实体从下往上；把中间 clip 拖到最上方空白行，row 变成最高+1；拖到最下方空白行，row 变 0。
- 同一帧给 x/y/rotation 打 key，改一次缓动，三条轨道 JSON 的 `easing` 相同；时间轴右键 clip 不出现缓动。
- 重力求解段：开始帧有普通 key；不填结束帧；物体落到 `physicsInteract=1` 地面后自动停；视口轨迹终点就是停止点；拖初速度手柄后 JSON `initialVelocity` 改变，松手一次 undo 回到拖动前。
- 求解段轨迹与属性面板 `SolverPreviewWidget` 的轨迹终点一致；预览与导出同一固定步长。

---

## 9. 最需要甲方拍板的 3 个点

### 决策 1：世界坐标到底采用哪套

- **A（推荐）**：画布中心原点、Y 向上、正旋转按 Mine-imator/视觉直觉；后端统一改。好处是符合用户当前“旧版坐标语义”的抱怨；代价是 `render2/anim/gizmo/solver/缓存/MCP` 全链改，测试量大。
- **B**：画布中心原点、Y 向下（保留旧数据方向），Gizmo 只把 Y 箭头画向上、拖动内部取负。改动小，但属性面板/MCP 的 Y 读数和视觉方向相反，容易再次混淆。
- **C**：保留左上原点，只给旧 UI 做偏移。改动最小，但 MCP/缓存/导出与 UI 坐标分裂，不推荐。

**推荐 A。** 需要确认：正旋转 90° 时 X 轴朝上还是朝下；这决定 `rotation` 符号和所有旧关键帧手感。

### 决策 2：“整体曲线”是“整帧缓动”还是“曲线图”

- **A（推荐）**：旧版实际在用的“当前帧所有轨道共用一次缓动”；恢复属性面板单按钮即可，后端已支持。
- **B**：还要恢复旧 `CurveEditor` 的多曲线图（X/Y/旋转/缩放/透明度同图）。这是额外 UI 工作，旧壳里本来就是 hidden，v1 可不做。

**推荐 A，B 延后。** 如果甲方说“我就是要看到所有曲线在一张图上”，再按 B 排期。

### 决策 3：求解结束帧的默认语义

- **A（推荐）**：`endFrame=-1` = 自动（物理停止/碰撞/片段末尾）；UI 默认只读显示自动结束；高级才手填。
- **B**：保留“-1 = 片段末帧”，另外加“自动停止”复选框；文件里仍写 -1，但 UI 明确区分“片段末尾”和“物理停止”。
- **C**：继续让用户选结束帧（不符合本次澄清，不推荐）。

**推荐 A。** 需要确认：公转/自转/黑洞没有碰撞，自动结束是否就等同于“片段末尾”；重力/黑洞的“实际停止”是否包含 `stopThreshold` 低速停止，还是只算碰撞。

---

## 10. 修复顺序建议

1. **P0-1 坐标原点 + P0-2 Gizmo 轴向**（同一套世界约定，必须一起改，否则会来回镜像）。
2. **P0-5 时间轴 bottom-up + 任意行拖动**（纯 UI 几何，先做测试再改交互）。
3. **P0-6 求解结束帧自动语义 + P0-7 视口轨迹/初速度手柄**（模型/求解器/视口一起；先做 `SolverPreviewWidget` 复用，再做 overlay）。
4. **P1-3 整帧缓动 + P1-4 缓动入口回属性面板**（低风险，紧随 P0）。
5. 最后补 P2：曲线图、样式、文案、测试截图。

---

## 11. 证据索引（按文件）

### 旧版（`git show legacy-ui-baseline:<path>`）

- `src/ui/Viewport.cpp:123-141`：中心原点画布、`screenToCanvas`。
- `src/ui/Viewport.cpp:710-718`：`worldToScreenTransform` 中心平移。
- `src/ui/Viewport.cpp:805-819`：旧 Gizmo 固定 X 右 / Y 上。
- `src/ui/Viewport.cpp:1580-1584`：拖 Y 用屏幕向下增量。
- `src/core/Project.cpp:874-890`：新建实体 `x=y=0`。
- `src/core/Project.cpp:1005-1012`：新建相机 `x=y=0`。
- `src/core/Camera.cpp:52-105`：相机中心与 `clampCenter` 以中心为基准。
- `src/render/SceneRenderer.cpp:28-44`：旧变换无 Y 翻转。
- `src/ui/PropertiesPanel.cpp:180-211`：旧缓动按钮按当前帧找任意 track。
- `src/app/MainWindow.cpp:3060-3088`：旧缓动一次应用到该帧所有轨道。
- `src/ui/CurveEditor.cpp:34-44`：旧曲线图六条曲线同图。
- `src/app/MainWindow.cpp:343-348`：旧交付壳把 CurveEditor/WaveformView 构造后直接 `hide()`。
- `src/ui/TimelinePanel.cpp:616-627`：旧时间轴 top-down。
- `src/ui/TimelinePanel.cpp:2130-2154`：旧时间轴右键无缓动。
- `src/ui/TimelinePanel.cpp:1929-1957`：旧双击 clip 只打 key。

### 新实现（当前工作树）

- `src/render2/Render.cpp:1222-1242`：左上原点 `canvasView`。
- `src/render2/Render.cpp:1411-1418`：`projectCanvas` 左上矩形。
- `src/render2/Render.cpp:1252-1262`：相机源矩形中心在相机 pose。
- `src/anim/Anim.cpp:108-121`：`composePose` 无中心偏移/无 Y 翻转。
- `src/ui/Viewport.cpp:469-477`：session 视口用 `canvas.left/top`。
- `src/ui/Viewport.cpp:689-758`：session Gizmo transform；solver 反推 rotation。
- `src/ui/Viewport.cpp:1393-1437`：Gizmo 绘制 X/Y 箭头。
- `src/ui/Viewport.cpp:1821-1865`：session 绘制顺序，无 solver overlay。
- `src/ui/WorkbenchPanel.cpp:1544-1644`：新建文字/矢量/相机不设 transform。
- `src/gizmo/Gizmo.h:29-31`：Y 向下/顺时针约定。
- `src/gizmo/Gizmo.cpp:179-204`：`handlePosition` 的 MoveX/MoveY。
- `src/ui/PropertiesPanel.cpp:1174-1175`：旧缓动按钮被隐藏。
- `src/ui/PropertiesPanel.cpp:3007-3026`：单轨 `sessionSetEasing`。
- `src/ui/PropertiesPanel.cpp:3303-3335`：逐轨缓动 combo。
- `src/ui/TimelinePanel.cpp:792-803`：top-down `rowTop/rowAt`。
- `src/ui/TimelinePanel.cpp:1869-1919`：session 拖动只落到已有 clip。
- `src/ui/TimelinePanel.cpp:3246-3259`：双击 key 直接弹缓动。
- `src/ui/TimelinePanel.cpp:3538-3562`：右键 key 的缓动菜单。
- `src/ui/TimelinePanel.cpp:4098-4114`：关键帧命中只看 X 距离。
- `src/model/Entity.h:18-27`：`endFrame=-1` 注释。
- `src/anim/Anim.cpp:17-23`：`effectiveEnd` 用 clip 末帧。
- `src/solver/Solver.cpp:22-28`：`-1` 解到 clip 末帧。
- `src/solver/GravitySolver.cpp:345-357`：最后一帧强制 `stopped`。
- `src/ui/PropertiesPanel.cpp:1341-1347`：结束帧 spin 默认 -1。
- `src/ui/PropertiesPanel.cpp:3524-3548`：已有 solve 卡片结束帧 + 初速度 spin。
- `src/next/properties/SolverPreviewWidget.h:15-25`：现成轨迹/手柄/提交说明。
- `src/next/properties/SolverPreviewWidget.cpp:22-28`：`kVelocitySeconds`/命中半径。
- `src/next/properties/SolverPreviewWidget.cpp:819-930`：拖手柄、写 `initialVelocity`、commit/失败恢复。
- `src/next/properties/PropertiesPanel.cpp:2342-2345`：新壳实例化 `SolverPreviewWidget`。
- `src/next/timeline/TimelineWidget.cpp:488-492`：bottom-up `rowTop` 参考。
- `src/next/timeline/TimelineWidget.cpp:2538-2545`：bottom-up `rowAt` 参考。

### 文档

- `docs/ENTITY_MODEL.md:246`、`:275-281`：row 0 最底、新行向上。
- `docs/ENTITY_MODEL.md:253`：X/Y 跟随实体旋转。
- `docs/ENTITY_MODEL.md:424-428`：结束帧/`-1` 旧语义。
- `docs/ENTITY_MODEL.md:437`、`:470`：求解轨迹预览 + 初速度手柄。
- `docs/USER_REQUIREMENTS.md:34`、`:56`、`:130-131`：bottom-up、轨迹/手柄、UI 逻辑保留、后端适配 UI。
- `docs/STATUS.md:38`、`:51-53`：bottom-up、求解段、预览在 Properties（本次澄清改为视口）。

---

## 12. 不做的事

- 不恢复旧 `.pvproj` 单文件兼容。
- 不把旧 `core::Project` 当第二数据源。
- 不在旧 UI 里另起一套新 Gizmo/时间轴；修的是旧 `src/ui` 的语义。
- 不为“曲线图”复制第二套缓动数学；先确认甲方是否要图。
- 不在求解手柄拖动过程中每帧 commit；一次拖动一次 commit。
