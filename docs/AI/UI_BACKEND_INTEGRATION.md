> **AI 文档（非用户文档）**：本文由 AI/代理生成，面向 AI 与开发者，可能滞后于代码。用户请以 `README.md`、`docs/USAGE.md`、`docs/FEATURES.md` 为准。

# 旧 UI 接入新后端方案

> 只读分析，未改代码、未提交。
> 基准：`new-entity-model` 分支 `94bc560 docs: make the legacy ui the delivery shell`；P0 冻结基线 tag `legacy-ui-baseline` = `ba07a59`。
> 目标：回答“怎么让旧 `src/app`、`src/ui` 跑在新实体模型 / Store / 渲染 / MCP / 工作台缓存上”，并保留旧 UI 的布局、面板、快捷键和交互。
> 产品名只叫 **PVForge**；最终可执行目标、窗口标题、安装包、MCP serverInfo 都不出现 `Next` / `pvforge-next`。
>
> **2026-11-10 P0 已落实**：`legacy-ui-baseline` tag 已打在 `ba07a59`；旧 `pvforge` 的 `QApplication` 名称/组织名/显示名已是 `PVForge`，旧 QSettings 键（`recentProjects` 等）保留。本轮不改 CMake 目标、不改 UI 布局/面板逻辑；`pvforge-next` 暂留作参考，不作为产品、不安装。

---

## 1. 结论

**推荐：混合式——旧壳原样保留，内部逐面板换内脏。**

具体说：

- **壳**：`src/app/MainWindow.*`、`WelcomeWindow`、`NewProjectDialog`、`ExportDialog`、`SettingsDialog`，以及 `src/ui/*` 的类名、控件树、布局、菜单、工具栏、浮动面板、信号签名和快捷键全部保留。交付目标最终叫 `pvforge`。
- **唯一数据源**：`model::Project` + `assets::AssetStore`，由 `store::ProjectStore` 落盘，由 `ProjectDocument` 实现 `next::Document` 接口（`src/next/core/Document.h`）提供提交、undo/redo、播放头和选择。旧 `core::Project` 不再是数据源。
- **适配层只做转换，不做第二份工程数据**：
  - `EditorSession`（新增，`src/app/EditorSession.*`）：`int` 视图 id ↔ `Uuid`、绝对帧 ↔ 相对帧、`model::Content` → 旧式只读 DTO `EntityView`；同时持有当前摄像机、私有时间线 owner、选择/播放头。
  - `EditActions`（新增，`src/app/EditActions.*`）：所有人类写操作的唯一入口。一次手势 = 一次 `commitEntity/commitChanges/commitProject`，失败返回错误，不直接改 store。
- **复用新后端里非 UI 的部分**：`ProjectDocument`、`MediaServices`、`WorkbenchServices`、`PlaybackController`、`next/settings`、`mcp2`、`export2`、`anim`、`render2`、`html2`、`video2`、`audio2`。
- **`src/next` 的面板只作行为参考**：`NextWindow`、`next/timeline/TimelineWidget`、`next/properties/PropertiesPanel`、`next/viewport/ViewportWidget`、`next/assets/AssetPanel` 不接入交付壳。
- **旧代码不删**：`src/app`、`src/ui` 原地迁移；`src/core/Project.*`、`core/History`、`core/Commands`、`render/*`、`html/*`、`media/VideoPlayer`、`mcp/*` 源码保留到收尾，只是从新目标链接列表移除。迁移期回退靠 git tag，不靠第二套可编译目标（见第 6 节）。

目标分层：

```
src/app + src/ui                 旧壳：旧类、旧布局、旧交互、旧信号
   │  读：EditorSession（EntityView / 视图 id / 绝对帧 / pose）
   │  写：EditActions（一次操作一次 commit）
   ▼
ProjectDocument                  提交、undo/redo、playhead、selection
   ▼
store::ProjectStore → model::Project + assets::AssetStore
   ▼
anim / render2 / html2 / video2 / audio2 / export2 / mcp2 / workbench
```

`ProjectDocument`、`MediaServices`、`WorkbenchServices`、`PlaybackController` 目前放在 `src/next/` 下。它们是**后端服务，不是新壳 UI**，可以暂留原路径（内部目录名不影响产品名），收尾时再决定是否迁到中性目录；不要因为路径里有 `next` 就把它们一起排除。

### 为什么不是“旧 UI 代码原样 + 纯适配层”

旧 UI 不是只读几个字段，它把旧数据结构当编辑器在用：

- 时间轴拖拽以整份 `QVector<Entity>` 快照为单位：`TimelinePanel::snapshotEditRequested(before, after, text)`，`MainWindow.cpp:638` 直接构造 `EntitiesSnapshotCommand`。
- 关键帧是**绝对帧、double**，clip 范围存在 `entity.data["clipStart"/"clipEnd"]`，音频起点/裁剪存在 `data["startFrame"]/trimStart/trimEnd`（秒）。
- `PropertiesPanel` 直接读写 `entity->data` 的 30 多个键；`Viewport` 直接调 `Project::value` 和 `SceneRenderer`。
- `AssetLibraryPanel`/`WorkbenchPanel` 依赖 `Template` 列表，模板存在工程 JSON 里。

如果给旧 `core::Project` 做一个门面、内部代理新模型，就必须保存一份影子 `Entity` 列表和旧的 `History` 命令栈。MCP 或 store 每次写完会 `notifyExternalChange()` 重新加载，影子立刻过期；两套 undo 也会互相踩。**看着省事，实际是最贵的方案。**

### 为什么不是“旧 UI 布局重写内部 / 直接接新面板”

那等于把 `src/next` 的新壳换个皮，违反 `USER_REQUIREMENTS.md` §9，也会丢掉旧 UI 19,781 行里真正值钱的交互：裁剪递归、私有时间线浮层、四角旋转/Office 对角缩放/pivot 手柄、吸附、监视器浮窗、MCP 日志与监视器的相对布局。新面板可以当行为基准，但不能当交付壳。

---

## 2. 旧 UI 资产清单

判定口径：

- **可保留**：类、控件树、绘制、事件处理基本不动。
- **需改内部**：布局/交互保留，数据访问、信号参数或渲染入口替换。
- **重写**：旧实现与旧模型绑死，只能保留外观/入口，内部重做。
- **退出交付**：旧壳里已隐藏或明确不做的功能，源码留到收尾再删。

### 2.1 窗口 / 对话框

| 组件 | 旧数据依赖 | 新后端对应 | 处理 |
|---|---|---|---|
| `MainWindow`（`src/app/MainWindow.*`） | `Project*` 的全部字段/方法；`History`；旧 `McpServer`/`McpCommandService`；旧 `html::HtmlRenderer`；`VideoPlayer`/`VideoProxyGenerator`；`AudioEngine`；`VideoExporter`；`PerfMonitor`（可保留）；`Settings`/`Lang` | `ProjectDocument`；`EditorSession`；`mcp2::Service`/`HttpServer`；`MediaServices`；`PlaybackController`；`export2`；`AppSettings` | **可保留（壳）**，内部接线需改；`Project::changed → refreshAll`（`MainWindow.cpp:598`）改为 `Document::changed` |
| `WelcomeWindow` | QSettings `recentProjects`；路径按单文件检查 | `ProjectStore::open`；工程是目录，检查 `project.json` | **需改内部**：最近 10 项仍保留，路径失效清理，打开旧单文件时提示“旧工程格式已废弃” |
| `NewProjectDialog` | 返回单个 `.pvproj` 文件路径；作者/描述 | `model::ProjectSettings`；`ProjectStore::create(dir)` | **需改内部**：改成“父目录 + 工程名 → 目录”；作者/描述去掉；fps/宽高创建后锁定 |
| `ExportDialog` / `ExportProgressDialog` | `const Project&`；`render/VideoExporter.h` 的 `ExportOptions`；整画布/工作相机/相机三选一 | `export2::Options`（cameraUuid 必填、start/end、outputFps 1x/2x/4x、resolutionScale、crf/encoder/preset/overwrite、`Progress(frame,total,eta)`、warnings） | **需改内部**：布局保留；视图下拉只留摄像机（整画布导出已取消）；宽高自由输入改成 1x/2x/4x；补 CRF/编码器/ETA/覆盖确认/取消 |
| `SettingsDialog` | `core::Settings`；语言包；解码信息页；MCP 场景授权/端口自动；自动保存 | `next/settings` 的 `AppSettings`（HTML/视频/工作台缓存/吸附/MCP/git/commit 延迟/自动保存）；`mcp2::HttpServer` 默认端口 47821，冲突报错不静默换 | **需改内部**：保留旧页签结构；删语言/主题；加 HTML/视频/Git/工作台缓存页；MCP 页改成端口/token/只读原因/急停；解码信息页可留作只读诊断 |
| `AppIcon` / `SystemInfo` | 无工程数据 | 无 | **可保留** |

### 2.2 面板 / 视图

| 组件 | 旧数据依赖 | 新后端对应 | 处理 |
|---|---|---|---|
| `TimelinePanel` + `ClipPopup` | `Project::entity/entities/mutableEntities`、`fps`、`timelineLength`、`isEffectivelyLocked/Hidden`、`setEntityParent`、`templates`、`createFromTemplate`、`addHtmlLayer`；`Entity.data["clipStart"/"clipEnd"/"startFrame"/"trimStart"/"trimEnd"/"duration"/"row"]`；`Entity.tracks`（绝对 double 帧）；`Entity.values`；`Entity.audio` | `model::Entity.start/end/row/parentUuid`；`model::Track`（相对 int 帧）；`anim::evaluateEntity`；`model::childrenOf`；`Document::commitChanges`；`assets` 音频峰值 | **需改内部 + 局部重写**：绘制、行命中、拖动手势、私有时间线浮层保留；模型访问、裁剪规则、关键帧帧基、快照类型、场景/标记带重做 |
| `PropertiesPanel` | `Entity.data`（`font/size/color/align/lineHeight/letterSpacing/stroke/fill/shape/cornerRadius/sides/innerRatio/width/height/...`）；`Entity.tracks/values`；`Project::value/previousKeyframeFrame/entityOrder/isDescendantOf`；`html::HtmlAsset` 的 components/variables | `model::TextContent/VectorContent/ComicTextContent/RadialLinesContent/FlowLinesContent/HalftoneDotsContent/BrandWallContent`；`model::Track` + `anim`；`assets::PvhtmlManifest`（存在 asset.json metadata） | **需改内部**：控件/分区保留；`dataEdited(QVariantMap)` 变 Content 字段映射；HTML 区从多 component 变单层扁平 `var.*`；新增求解段分区、visible/inheritVisibility/physicsInteract、负 scale |
| `TextLayoutDialog` | `core::TextStyle` / `TextBlock` | `model::TextContent` + `render2` 的文字测量 | **需改内部（小）**：字段一一映射；预览走 render2 同一套绘制 |
| `EasingPicker` / `EasingChooser` / `BezierEditorDialog` | `core::EasingSpec`（enum `Easing`） | `model::EasingSpec`（已经是 30 预设 + linear/instant/bezier，`model/Easing.h`） | **需改内部（小）**：换类型和曲线绘制函数即可，UI 不动 |
| `AssetLibraryPanel` | `Project::templates()/templateAt/removeTemplate/addImportedMedia/createFromTemplate/importHtmlAsset/addHtmlLayer`；`Template{type,name,path,duration,data,thumbnail,audio}`；拖拽 mime `application/x-pvforge-asset-template` + index | `assets::AssetStore::list/get/getIncludingMissing/importFile/importVideoWithAudio/importPvhtml/setTags/remove`；拖拽 mime 改成 `application/x-pvforge-asset` + hash | **需改内部**：列表/搜索/图标/悬停大图保留；模板索引换 hash；缺失素材保留并标红；视频拆 WAV 后的进度接 `video2` |
| `Viewport` | `Project::value/entity/entities/entityOrder/effectiveOpacity/isEffectivelyHidden/isEntityActiveAt/activeCamera/background`；`Entity.data/values/tracks/hidden/locked/image`；`SceneRenderer::drawEntity/entityLocalBounds/worldTransform`；旧 `Camera::State/clampCenter`；旧 gizmo 数学 | `render2::renderFrame` + `anim::evaluateEntity` + `SceneWorld`；`gizmo::localMatrix/hitTest/handlePosition`；`WorkbenchServices::lookup`；`EditorSession::pose` | **需改内部 + 局部重写**：相机取景框、吸附、手柄绘制/命中保留；内容绘制、命中、变换换 render2/gizmo，选中框/Gizmo/参考线叠在缓存图或实时帧上 |
| `CameraMonitor` | `Project::monitorView/activeCamera/width/height/fps`；`CameraRenderer::renderWorkCamera/renderCameraImage`；`VideoPlayer` + 临时 `Project` 做临摹视频 | `render2::renderFrame`（cameraUuid）或相机视图；`video2::VideoFrames` 做临摹；无 HTTP/RTP | **需改内部**：浮窗/拖出/缩放/标题栏/临摹透明度保留；删串流菜单；几何入 QSettings |
| `McpLogPanel` | `addToolInvocation(name,args,result,ms,ok)`；`setPermissionState(armed, sceneName, stopped)`；`addSystemEvent` | `mcp2::HttpServer::toolCallStarted/toolCallFinished/toolInvoked/stoppedChanged`；`Service::requestStop/resumeWrites/writesEnabled` | **需改内部**：面板、行上限、拖出、急停按钮保留；状态从“场景授权”改“只读/AI 修改中/已急停”；保留 `MainWindow::arrangeMcpLog()` 的“监视器旁浮窗”布局，不做底部 dock |
| `WorkbenchPanel`（旧：浮窗创建面板） | `Project::templateAt/templateCount/createFromTemplate/addEntity/addText/addVector/addCamera/addBrandWall`；`core` 特殊内容绘制 | `assets::AssetStore`；`model::ContentKind`；`Document::commitEntity`；`render2` 预览 | **需改内部**：所有表单和浮窗交互保留；创建改走 `EditActions::createEntity`；特殊内容参数映射到 Content 结构 |
| `BrandWallDialog` | `core::BrandWallParams`（width/height/gap/logoOpacity/logos/tileColors） | `model::BrandWallContent`（columns/rows/spacing/logoSize/colors/alternate/angle/background/seed；图片/SVG logo 尚未迁） | **需改内部 + 部分延期**：数值参数可映射；图片 logo 需要扩 Content（见第 3 节） |
| `ProjectPropertiesPanel` | `name/author/description/width/height/fps` | `model::ProjectSettings.name/width/height/fps`（背景色在 `EnvironmentPanel`） | **需改内部**：名称可改并 `commitProject`；宽高/fps 只读；作者/描述去掉 |
| `EnvironmentPanel` | `background`（`QColor`） | `project.settings.background`（`#rrggbb` 字符串） | **需改内部（小）**：颜色选择 + `QColor`/十六进制转换 + 一次 `commitProject` |
| `CurveEditor` / `WaveformView` | 旧 `Project` 曲线/波形；旧壳已 `hide()` | 无 v1 需求；音频峰值应在时间轴条上 | **退出交付**：源码留到收尾；不接主布局 |
| `CollapsibleSection` / `AppIcon` | 无工程数据 | 无 | **可保留** |

### 2.3 旧底层资产（不作为新数据源）

| 旧模块 | 现用途 | 新对应 | 处理 |
|---|---|---|---|
| `core/TextStyle.*`、`core/TextBlock.*`、`core/ComicBox.*`、`core/LineDeco.*`、`core/BrandWall.*`、`core/VectorShape.*` | 文字测量、漫画框/线条/品牌墙的预览绘制 | `model::*Content` + `render2` | **仅作几何/预览 helper 保留**；数据真源换成 Content；能走 render2 的预览统一走 render2 |
| `core/Camera.*` | 相机取景框和监视器 | `anim` 的相机 pose + render2 的 cameraView | **退出数据路径**；取景框数学按新 `gizmo`/`anim` 重算 |
| `core/History.*`、`core/Commands.*`、`core/CheckpointManager.*` | 旧 undo/场景 checkpoint | store 内存 undo + git commit + 历史面板 | **退出交付路径**；源码留到收尾 |
| `core/Settings.*`、`core/Lang.*` | QSettings 包装、49 个 key 的语言包 | `next/settings` 的 `AppSettings`；`tr()` | **Settings 逐步并入 AppSettings；Lang 放弃** |
| `html/HtmlAsset.*`、`html/HtmlRenderer.*`、`html/HtmlBrowser.*` | 按层 CDP、多 component 变量 | `html2` + `assets::PvhtmlManifest` + `MediaServices` | **退出交付路径**；`.pvhtml` 导入和变量从 asset metadata 读 |
| `media/VideoPlayer.*`、`media/VideoProxy.*`、`media/MediaImporter.*` | 旧解码/代理/导入 | `video2` + `assets::AssetStore` + `MediaServices` | **退出交付路径** |
| `render/SceneRenderer.*`、`render/CameraRenderer.*`、`render/VideoExporter.*` | 旧预览/相机/导出 | `render2`、`export2` | **退出交付路径** |
| `mcp/McpServer.*`、`mcp/McpCommandService.*` | 旧工具集、场景授权、checkpoint | `mcp2` 46 工具、git 只读、急停、写锁 | **退出交付路径** |

---

## 3. 最大阻塞点（按严重度）

### P0-1 时间轴的模型假设整体不兼容

旧时间轴不是“字段名不同”，而是几层语义同时不同：

- **关键帧帧基**：旧 `Entity.tracks[prop].keys[].frame` 是绝对项目帧、`double`；`Project::value(id, prop, frame)` 直接按绝对帧求值。新 `model::Track` 的 key 是相对 `entity.start` 的整数，只允许 `0..duration-1`，裁剪 start 时 key 要平移/删除（`ENTITY_MODEL.md` §5）。旧 `TimelinePanel::keyframeFrames/cleanKeys` 可以继续看 DTO 转好的绝对帧，但 `ClipPopup` 的编辑、`moveKeys` 和所有写回都必须换算成相对帧。
- **clip 范围**：旧普通实体存在 `data["clipStart"/"clipEnd"]`，音频存在 `data["startFrame"]`（秒）+ `trimStart/trimEnd`（秒）+ `duration`（秒）；`TimelinePanel::audioMoved(EntityId, double startFrame)` 用的也是秒。新模型是 `entity.start/end`（整数帧）+ `content.asset.sourceIn`（帧）+ `volume`。
- **行号**：旧 `data["row"]` + `assignMissingRows`；新 `entity.row` 是每个父级独立的行空间，新建规则是“整个 `[start,end)` 范围最高占用 row + 1”。
- **父子**：旧 `Entity.parent`（int，同一扁平列表）；新 `parentUuid`，子实体 `start/end` 必须在父范围内，换父级要问“保持世界位置/保持局部坐标”，越界拒绝。
- **拖拽快照**：旧一次拖拽复制 `QVector<Entity>` 到 `m_before/m_preview`，松手发 `snapshotEditRequested(before, after, text)`，`MainWindow.cpp:638` 压入 `EntitiesSnapshotCommand`。新 store 没有“整体实体列表”概念，正确写法是 `Document::commitChanges(upserts, deletes, message)`，并且**一次手势一个 commit**。
- **裁剪递归**：旧 `TimelinePanel::trimStartRec/trimEndRec` 按 clip 边界级联；新规则还要处理求解段：裁 start 切进段要删段并把新起点的解算位姿落成普通 key，裁 end 切进段要把结束标记冻结在新末帧（`ENTITY_MODEL.md` §5.1 关键帧规则、§5.4 求解关键帧）。旧代码完全不知道 solver。
- **场景/标记**：旧 `Scene` 概念删除；旧时间轴的 scene band/annotation lane 本来就是空实现。新 markers 要放在标尺下的 marker lane，支持增删改、拖动、颜色/备注/标签。

影响：`TimelinePanel.cpp`（2,477 行）里约 600–900 行模型访问/裁剪/关键帧逻辑要重写，绘制和手势可保留；`MainWindow.cpp` 的时间轴信号处理约 300 行要改。

处理方向：`EditorSession::entityView(id)` 给时间轴一份**只读 DTO**，把新模型的相对 key 转成绝对帧供旧绘制代码使用；所有写回走 `EditActions`，按新规则校验。不要试图把 `data["clipStart"]` 翻译成 `start/end` 后继续用旧裁剪函数。

### P0-2 旧 Project 单文件与工程生命周期

- 旧 `Project::save/load(filePath)` 是单个 `.pvproj` JSON；`MainWindow` 里 `m_project->filePath` 出现 20 次，用于标题、最近项目、素材目录、导出默认路径、播放头 QSettings key、打开工程文件夹。
- 新工程是 `.pvproj/` 目录：`project.json` + `entities/<uuid>.json` + `assets/` + `.cache/`；`ProjectStore::create` 要求目录为空（或不存在）。
- 旧 `packAssets`/`copyIntoProject`/`assetDirectory` 的“素材打包开关”在新模型里没有意义：素材天然在工程 `assets/`，不进 git；`AssetStore` 用 sha256 内容寻址。
- 旧保存/自动保存是写单文件；新是 debounce commit + `Document::flush()`。`Ctrl+S` 应变成 flush；`Ctrl+Shift+S`/“另存为”要么删、要么实现“复制工程目录”，不能保留旧语义。
- 播放头旧的存 `QSettings`（按文件路径）；新 `ProjectDocument` 存 `<root>/.pvforge/playhead.json`。
- 打开工程还要处理崩溃恢复：`ProjectStore::dirtyText()` + `commitRecoveredText()/discardDirtyText()`。旧 UI 没有对应弹窗，需要补一个恢复/丢弃/提交对话框。

处理方向：`EditorSession` 持有 `ProjectDocument`，所有 `filePath` 用法改成 `document->root()`；`NewProjectDialog`/`WelcomeWindow` 按目录改；`closeEvent` 先 `flush()` 再删服务。

### P0-3 旧命令/undo 与 store undo 不能并存

- `MainWindow.cpp` 有 33 处 `m_history->push(...)`，`CurveEditor.cpp` 2 处，旧 `McpCommandService.cpp` 5 处。命令的 `undo/redo` 直接调 `Project&` 的 `setValue/insertEntity/setEntities/...`。
- 新 store 的 undo 是**文本文件字节快照**：每次 commit 记录 `UndoEntry{before, after}`，`ProjectDocument::undo()` 写回快照并重新加载模型。它和旧 `History` 是两套完全不同的栈。
- 更麻烦的是**合并编辑**：旧 `SetValueCommand::canMergeWith/mergeWith` 让连续拖 spinbox 合并成一步；新 store 每次 commit 都是一步。如果 `PropertiesPanel` 的 `valueChanged` 每动一格就 commit，200 步 undo 很快被一次拖动塞满。时间轴反而没问题，它本来就是松手一次快照。
- 旧 `undoAction` 的菜单文字来自 `History::undoText()`（“撤销 修改属性”）；store 没有按操作的文案。

处理方向：

- 删除交付路径里的 `History`/`Commands`，`Ctrl+Z/Ctrl+Shift+Z` 直接调 `Document::undo/redo`；
- `EditActions` 负责把连续属性编辑合并：spinbox 用 `editingFinished` 或 300–500 ms 合并定时器，时间轴/视口用“按下开始、松手 commit”；
- 菜单文案由 `EditActions` 自己维护一个“最近操作标签”栈（只影响显示，不参与数据），不要为了文案恢复旧 `History`；
- 所有写操作都走 `EditActions`，面板里不允许直接 `mutableProject()` 改完不 commit；否则下一次 `notifyExternalChange()` 会把内存改动覆盖掉。

### P0-4 渲染、视口与 Gizmo 的坐标语义

- 旧视口是“逐实体画”：`Viewport::paintEvent` 按 `Project::entityOrder()` 逆序调 `SceneRenderer::drawEntity`，选中框画在 `drawEntity` 里，再画相机取景框、参考线、Gizmo。新 `render2::renderFrame` 一次画整帧（背景 + 内容 + 相机取景），需要 `anim::SceneWorld` + `FrameProvider`。
- 变换公式不同：旧 `SceneRenderer::entityTransform` 是 `T(x,y) · R · T(-rotPoint) · S`；新 `gizmo::localMatrix` 是 `T(x,y) · T(pivot) · R · S · T(-pivot)`。旧 `x/y` 是旋转锚点，新 `x/y` 是平移量，`pivot` 是局部锚点。**不能把 `rotPointX/Y` 直接赋给 `pivotX/Y`**；换算大致是 `new.x = old.x - old.rotPointX`、`new.pivot = old.rotPoint`，缩放时还要按新公式重算，并用像素对拍验证。
- 旧相机 `Camera::state` 用 `scaleX` 表示“画布比例”，`clampCenter` 保证取景框在画布内；新相机是普通实体，`anim::evaluateEntity` 给世界矩阵，render2 用四个画布角点做 `quadToQuad`。旧 `CameraRenderer::renderCameraImage`/`renderWorkCamera` 不能再用。
- 旧视口命中测试用 `SceneRenderer::entityLocalBounds`；新要按 Content 算 bounds（next ViewportWidget 的 `gizmoBounds` 是现成参考：文字用 `TextBlock::measure`，图片/视频用 asset 元数据，文字/矢量/特殊内容各自的本地框）。
- 旧 Gizmo 数学（轴长、四角旋转、Office 缩放、中心四象限、pivot 拖动）与 `src/gizmo` 是两套；必须换成 `gizmo::handlePosition/hitTest/localMatrix`，否则手柄和 render2 画出来的实体对不上。
- HTML 是异步的：`MediaServices::htmlFrame()` 只查缓存，miss 返回空并在后台渲染，完成后发 `htmlFrameReady`。旧视口在 `paintEvent` 里同步拿图，必须改成“miss 先画占位，收到信号再 `update()`”。
- 工作台缓存：旧视口不知道缓存存在。新逻辑应先 `WorkbenchServices::lookup(frame, cameraUuid, &hit)` 取整帧 WebP，命中直接贴图；miss 再 `render2` 实时画；Gizmo/选中框/参考线不进缓存。画布视图用扩展矩形（默认 50%，透明底），相机视图和导出不扩展。

处理方向：先做“视口只读渲染 + 播放”，再做“Gizmo 编辑 + 吸附”。渲染和导出的首帧用同一 `render2` 对拍，避免两套画法。

### P0-5 MCP 的权限模型与双写

- 旧 `McpServer` + `McpCommandService` 是 2,389 + 1,773 行的旧工具集，权限是“人类按场景 arm 写入 + checkpoint”，`McpLogPanel::setPermissionState(armed, sceneName, stopped)` 就是这套。
- 新 `mcp2` 是 46 个工具、`writesEnabled`（Git 关闭 => 只读）、`requestStop/resumeWrites`、每次写一个 commit、`isWriteTool()`、`setBatchCheckpoint()`。
- 新要求：AI 写期间锁人类编辑，只读/预览/查询不锁；写完成必须刷新时间轴/视口/属性/素材/工作台并失效缓存扫描。
- 旧 UI 的面板没有“AI 忙”概念；直接 `setEnabled(false)` 会把选择、滚动、播放也锁掉。

处理方向：

- `MainWindow` 接 `mcp2::HttpServer` 的 `toolCallStarted/toolCallFinished/stoppedChanged`；`McpLogPanel` 只改状态来源和按钮语义；
- `EditorSession` 增加 `humanEditingEnabled` 标志，`EditActions` 在入口拒绝写；面板的编辑控件（spinbox/按钮）按该标志禁用，导航/播放保持可用；
- 写工具完成后 `ProjectDocument::notifyExternalChange()` + `refreshAll()`；只读工具不刷新全量；
- 提供 `Service::setViewStateProvider`：播放头、选择 uuid、私有时间线 owner、当前相机、视口范围/缩放；
- 提供 `setVideoFrameProvider/setHtmlFrameProvider` 给 `preview.render_frame`，走 `MediaServices`（编辑器和 MCP 共用同一套 provider，不能另起解码）。

### P1-6 导出链路

- 旧 `ExportDialog` 持 `const Project&`，输出 `render/VideoExporter.h` 的 `ExportOptions`；旧 `MainWindow::exportVideo` 自己管 `VideoExporter` 和 HTML 成品渲染。
- 新 `export2::Options` 要求 `cameraUuid`，范围是相机 `[start,end)`；支持 2x/4x fps、1x/2x/4x 分辨率、CRF、libx264/h264_vaapi、preset、overwrite、`Progress(frame,total,eta)`、warnings；HTML 用第三浏览器高清逐帧，视频用原片。
- 旧对话框的“工作相机/整画布”导出要删；宽高自由输入与“1x/2x/4x”冲突，建议换成下拉。
- 导出必须用 `MediaServices::exportVideoFrame/exportHtmlFrame`，不能复用工作台缓存（缓存是编辑器预览）。
- `export2` 是同步调用，`NextWindow` 用 `QtConcurrent` 包了一层；旧 `MainWindow` 的导出 worker 线程/取消/进度对话框要照这个模式重写。

### P1-7 素材与 Template

- 旧 `Template` 是工程内的媒体库条目（`name/path/duration/data/thumbnail/audio`），`Project::createFromTemplate` 负责建实体；`AssetLibraryPanel` 拖拽 mime 带 template index。
- 新素材是 `AssetStore` 的内容哈希条目（无 UUID、sha256、`assets/<hash>/content.*`）；实体只引用 `content.asset.hash/type`。没有“模板”概念，素材箱列表来自 `assets.list()/listIncludingMissing()`。
- 视频导入会自动拆 WAV，返回视频 + 音频两个 hash；旧 `addImportedMedia` 是加两个 template。拖入时间轴要生成两个实体。
- 拖拽 mime 要统一成 `application/x-pvforge-asset` + hash；`TimelinePanel::dropAsset` 和 `AssetLibraryPanel` 两端都要改。
- 缺失素材：`getIncludingMissing` + `missingContent`，视口显示 render2 的“素材丢失”卡片，属性面板显示缺失状态；重新导入同 hash 自动恢复。

### P1-8 内容字段、特殊内容与相机

- 旧 `Entity.data` 是万能 `QVariantMap`，`PropertiesPanel` 有 30+ 键；新是强类型 `Content` 结构（文本/矢量/相机/素材/漫画/径向线/流线/点阵/品牌墙）。
- 旧相机 `scaleX` 是画布比例；新相机是普通 transform，scale 绝对值限制 0.05..1；相机可以当父级/子级，旧 `Camera::State` 没有这个。
- 旧 `EntityType::Folder` 在新模型里不存在：空实体就是无内容实体。旧的“文件夹”行、成组、文件夹 gizmo 要映射成 `ContentKind::Empty` + `parentUuid`，不能靠类型判断。
- 品牌墙：新 `BrandWallContent` 只有文字标签 `logos` 列表，没有旧 `BrandWallParams` 的图片/SVG 贴图、width/height/gap/logoOpacity。这是已知缺口，需要扩 Content 或明确延期。
- 音频：旧 `data["startFrame"]`（秒）、`trimStart/trimEnd`（秒）、`fadeIn/fadeOut`；新只有 `start/end`、`sourceIn`（帧）、`volume`（可打 key）。淡入淡出 v1 没有字段。

### P1-9 相机监视器、HTML 与工作台缓存

- 旧 `CameraMonitor::renderPreview` 分“工作相机/整画布/场景相机”，用 `CameraRenderer`；新只能按相机 uuid 走 render2，工作相机视图要么映射成“无相机画布视图”，要么删掉这个模式。
- 临摹视频旧实现是“临时 `Project` + `VideoPlayer` + 一个 trace entity”；新可以直接用 `video2::VideoFrames` 按文件解码，不需要造工程。
- 串流（HTTP/RTP）按需求删除；`CameraMonitor` 里串流相关代码（菜单位项、ffmpeg 进程、HTTP 服务）删除/停用，但浮窗、拖出、缩放、透明度保留。
- HTML 旧是 `html::HtmlAsset`（多 component、多变量）+ 每层 `HtmlRenderer`；新是 `.pvhtml` 单入口 + `PvhtmlManifest` 扁平 `variables`，`MediaServices` 管三个浏览器和缓存。`PropertiesPanel` 的 HTML 变量区要从“component.variable”改成“扁平 var.*”。
- 工作台缓存的服务是“每相机一个 `WorkbenchServices`”；旧 `Viewport` 画布视图用空 cameraUuid，监视器用当前相机，需要 `EditorSession` 管一组实例，并在相机/设置/文档变化时重建/失效。

### P1-10 设置、ID、剪贴板、场景残留、入口

- 设置：旧 `core::Settings` 的键（语言、自动保存、打包、默认分辨率/fps、时间轴缩放、MCP 端口自动、解码页）与新 `AppSettings` 只有部分重合。吸附四项几乎一一对应（`snapPixels/edgeCenter/edgeMidpoint/snapAngles`），可直接搬；自动保存改成 commit debounce + 5 分钟 flush；语言包放弃。
- 快捷键：旧 `Ctrl+E/F/D/C/V/Space/Home/End/Ctrl+,/Ctrl+Shift+P` 全部保留；`Ctrl+S` 改成 `flush()`；`Ctrl+Shift+S`（另存为）建议删除或改成“复制工程目录”。
- ID：旧 `EntityId=int` 贯穿所有面板信号、剪贴板、场景、MCP 日志跳转；新是 `Uuid=QString`。第一版建议保留 int 信号，由 `EditorSession` 做稳定映射（会话内不复用已删除 id），避免改 8 个面板的全部信号签名。
- 剪贴板：旧 `MainWindow::EntityClipboard` 存子树 + `pairedVideo`；新模型没有 pairedVideo，视频/音频是两个独立实体。复制/粘贴/原地复制要重写为“选中实体（可选子树）+ 新 UUID + 一次 commit”，不能直接复用旧结构。
- 场景残留：`MainWindow` 的 `SceneDialog`/`AnnotationDialog`、`TimelinePanel` 的 `sceneCreateRequested/...` 信号要删；`Annotation` 改成 `model::Marker`，旧对话框可以改造成 marker 编辑器。
- 入口：旧 `src/main.cpp` 有 13,000+ 行，混着大量旧模型自测；交付入口要换成 slim main（`WelcomeWindow` + `MainWindow(EditorSession)`），旧自测冻结在 tag，不能拖住新目标编译。

---

## 4. 分阶段计划

> 原则：每个阶段结束时交付目标 `pvforge` 都能启动；面板按“先只读、再编辑”推进；每个面板迁移完立刻用真实工程验证，不攒到最后。

### P0 基线冻结与目标命名（S，0.5 天）

- 目标：把“旧 UI 是交付壳”固化，避免迁移中双目标漂移。
- 动作：
  - ✅ 已打 annotated tag `legacy-ui-baseline`（指向 `ba07a59`）；
  - 交付目标保持 `pvforge`（旧 `src/app` + `src/ui` + 新模块）；`pvforge-next` 只作参考且不安装。P0 暂不做 CMake 目标清理/改名；内部临时目标名不得出现在窗口/安装包/MCP serverInfo；
  - ✅ `QApplication::setApplicationName("PVForge")`、`setOrganizationName("PVForge")`、`setApplicationDisplayName("PVForge")`；沿用旧 QSettings 键（`recentProjects` 等），避免升级后最近项目和界面设置丢失；
  - ✅ 更新 `docs/AI/STATUS.md`、`docs/AI/INTEGRATION_PLAN.md`、`docs/AI/OLD_UI_FEATURES.md` 中“新壳是交付、旧 app 最终删除”的过时描述；同时修正 `ENTITY_MODEL.md`/`STATUS.md` 里“MCP 日志底部 dock”的写法，按 `USER_REQUIREMENTS.md` 和旧代码 `arrangeMcpLog()` 改为“监视器旁浮窗”。
- 里程碑：✅ `legacy-ui-baseline` tag 存在（`ba07a59`）；构建文档写清“交付目标 = 旧壳 + 新后端”。

### P1 会话层与工程生命周期（M，3–4 天）

- 目标：旧 `MainWindow` 能新建/打开/关闭新格式工程，工程属性和背景色可用。
- 文件：新增 `src/app/EditorSession.{h,cpp}`；改 `src/app/MainWindow.{h,cpp}` 构造函数/`refreshAll`/`updateWindowTitle`/`closeEvent`/`saveProject`；改 `WelcomeWindow`、`NewProjectDialog`、`ProjectPropertiesPanel`、`EnvironmentPanel`；新增 slim `src/main.cpp`。
- 内容：
  - `EditorSession` 持 `ProjectDocument` + `AppSettings` + 视图 id 映射 + 当前相机/时间线 owner；
  - `MainWindow` 不再 `new Project`，改为接收 `EditorSession*`；**时间轴/视口/属性/素材箱/监视器在 P1 先不实例化**（中央区放只读占位页），P2–P5 逐个换成 `EditorSession*` 后再加回布局；不要为了“先跑起来”再造一份影子 `Project` 喂旧面板；
  - 新建工程 = `ProjectStore::create(dir, settings)`；打开 = `ProjectStore::open(dir)`；旧单文件路径提示废弃；
  - `Ctrl+S`/关闭 = `Document::flush()`；标题显示工程名 + 未提交状态；
  - 播放头存 `.pvforge/playhead.json`；
  - 最近项目按目录校验 `project.json`。
- 里程碑：启动 `pvforge` → 欢迎页 → 新建 `/tmp/demo.pvproj/`（1920×1080@30）→ 主窗口标题 `PVForge — demo`，工程属性显示名称/分辨率/fps，背景色可改并落盘；关闭再打开数据仍在；旧 `.pvproj` 文件被拒绝并提示。

### P2 时间轴（L，5–7 天）

- 目标：旧时间轴完整跑在新模型上，先只读显示，再编辑。
- 文件：`src/ui/TimelinePanel.{h,cpp}`、`ClipPopup`；`src/app/MainWindow.cpp` 时间轴信号段；新增 `EditActions`；`EditorSession` 的 `EntityView` 转换。
- 内容：
  - P2a 只读：实体行、开始/结束、父子缩进、row、名称、锁定/隐藏、音频峰值、当前帧、播放头、私有时间线打开；状态栏“总长”改根实体最大 `end`（`PlaybackController::contentEnd()`），空工程显示开放时间轴，不再用旧 `Project::timelineLength()`；
  - P2b 编辑：拖动/裁剪（含子树级联、父范围约束、求解段裁剪规则）、换行/换父级（保持世界位置/局部坐标询问）、删除级联、关键帧打/移/删/复制粘贴、合并菱形、S 切分、marker 增删改/拖动；
  - 一次手势一个 commit；撤销/重做走 `Document`；连续属性编辑合并。
- 里程碑：打开 `STATUS.md` §3.1 那类 7 实体工程（摄像机/文字/矢量/图片/视频/音频/HTML），旧时间轴显示 7 行且顺序/范围正确；拖动音频条后 `entities/<uuid>.json` 的 `start/end` 变化；在 60 帧打 key 后 key 的相对帧 = `60 - start`；`Ctrl+Z` 一次回到拖动前；私有时间线浮层能编辑子实体。

### P3 视口、播放、Gizmo、相机监视器（L，5–7 天）

- 目标：视口画面与 render2 一致，能播放，能拖 Gizmo，监视器能看相机。
- 文件：`src/ui/Viewport.{h,cpp}`、`src/ui/CameraMonitor.{h,cpp}`、`MainWindow` 播放/相机段；`MediaServices`、`WorkbenchServices`、`PlaybackController` 接线。
- 内容：
  - P3a：`render2::renderFrame` 整帧渲染 + `MediaServices` 视频/HTML provider + 工作台缓存 lookup；播放用 `PlaybackController`（帧时钟，音频只出声）；HTML miss 占位 + `htmlFrameReady` 刷新；
  - P3b：Gizmo 换 `src/gizmo` 数学；命中/边界按 Content 计算；吸附接 `AppSettings`；相机取景框/监视器按 anim pose + render2；临摹视频接 `video2`，删串流；
  - 服务归属和销毁顺序：`MediaServices`/`WorkbenchServices` 属于 `EditorSession`，必须先于面板和 `ProjectDocument` 销毁；旧的 `cleanupHtmlRenderers()`/`m_htmlBrowser` 整套删除，不能和新三浏览器并存。
- 里程碑：同一帧、同一相机、同尺寸下，视口截图（隐藏 Gizmo/选中框）与 `preview.render_frame` PNG 像素一致；拖 Gizmo 旋转 30° 后 JSON `rotation=30` 且画面同步；父级移动子级跟随；播放 90 帧不崩、音频出声；工作台缓存 Debug 显示命中数 > 0；监视器拖出浮窗后画面与相机导出首帧一致。

### P4 属性、内容、缓动、特殊内容（M，4–5 天）

- 目标：旧属性面板完整编辑新 Content，特殊内容一等公民。
- 文件：`src/ui/PropertiesPanel.{h,cpp}`、`TextLayoutDialog`、`EasingPicker/Chooser/BezierEditorDialog`、`WorkbenchPanel`（创建）、`BrandWallDialog`。
- 内容：
  - `EntityView` 暴露 Content；`dataEdited(QVariantMap)` 在 `EditActions` 里映射到 `TextContent/VectorContent/ComicTextContent/RadialLinesContent/FlowLinesContent/HalftoneDotsContent/BrandWallContent`；
  - 变换区注意 pivot 语义（`rotPointX/Y` → `pivotX/Y` 的换算）；
  - 关键帧区菱形按相对帧；auto-key 默认开；33 项缓动接 `model::EasingSpec`；
  - HTML 区接 `PvhtmlManifest.variables`（asset metadata），`var.*` 可打 key；
  - 新增求解段分区（效果、参数、轨迹预览、初速度手柄、段内轨道锁定）；
  - visible/inheritVisibility/physicsInteract、负 scale、锁定/隐藏继承；
  - `Document::changed` 会全量刷新，必须区分“自己刚写回显”和“MCP/外部刷新”，避免打断正在拖的 spinbox、颜色对话框或文字输入（QSignalBlocker + 焦点/编辑状态判断）。
- 里程碑：选中文字改字号/颜色/描边，JSON `content.text.*` 变化且当前帧渲染同步；33 个缓动都能选并求值；漫画框/径向线/流线/点阵/品牌墙能创建并改参数；HTML 变量能打 key 并驱动画面；求解段能创建/裁剪/删除并给出正确位姿。

### P5 素材箱、导入、替换、缺失（M，3–4 天）

- 目标：旧素材箱跑在 `AssetStore` 上。
- 文件：`src/ui/AssetLibraryPanel.{h,cpp}`、`WorkbenchPanel::pickMedia/refreshMediaLibrary`、`MainWindow::importMediaFiles/importImageFile/replaceSourceFromDialog`。
- 内容：
  - `assets.list/listIncludingMissing` 填列表；缩略图从 `asset.directory/content.*` 解码；搜索/标签/改名/删除；
  - 导入图片/视频/音频/`.pvhtml`；视频拆 WAV 后显示进度/取消（`video2` 队列）；
  - 拖拽 mime `application/x-pvforge-asset` + hash；`TimelinePanel::dropAsset` 按鼠标帧/行创建实体；
  - 换素材按兼容组（视觉/生成内容/音频/相机/空实体）；短素材自动裁 end、越界 key 删除；
  - 缺失素材显示 render2 离线卡片；重新导入同 hash 恢复。
- 里程碑：导入一段带音轨视频后 `assets/` 出现视频 + WAV 两个 hash；拖入时间轴生成视频 + 音频两个实体；把长视频换成短图后实体 end 自动裁短且越界 key 消失；删除素材后视口显示“素材丢失”，重新导入同文件后恢复。

### P6 MCP、Git、设置（M，3–4 天）

- 目标：46 工具、急停、写锁、可选 Git、设置页跑通。
- 文件：`MainWindow` MCP/Git 段、`src/ui/McpLogPanel.{h,cpp}`、`SettingsDialog`；新增/复用历史面板；`EditorSession` 的 `viewStateProvider`。
- 内容：
  - `mcp2::Service` + `HttpServer` 固定端口 47821、token 持久化、端口冲突报错；
  - `McpLogPanel` 接 `toolCallStarted/Finished/stoppedChanged`；状态改“只读/AI 修改中/已急停”；急停/恢复按钮接 `requestStop/resumeWrites`；
  - AI 写期间 `humanEditingEnabled=false`；写完成 `notifyExternalChange` + `refreshAll`；
  - Git 默认关；关闭时 `writesEnabled=false`，写工具 `permission_denied/git_disabled`；首次引导；崩溃恢复弹窗；历史面板（git 开启时）恢复提交；
  - 设置页接 `AppSettings`：HTML/视频/工作台缓存/吸附/MCP/git/commit debounce/自动保存。
- 里程碑：MCP `tools/list` 返回 46 工具；写工具执行时旧 UI 编辑控件禁用、时间轴/视口仍可看；急停后写工具返回 `stopped_by_human`；重启后 token/端口不变；Git 关闭时写工具失败且工程可正常保存；Git 开启后历史面板能恢复某个 commit；设置重启后仍生效。

### P7 导出、清理、验收（L，4–5 天）

- 目标：导出端到端，旧管道退出，产品名统一。
- 文件：`src/app/ExportDialog.*`、`ExportProgressDialog.*`、`MainWindow::exportVideo`；CMake；删除/停用旧 `render/html/media/mcp/core` 链接。
- 内容：
  - 导出对话框接 `export2::Options`：摄像机必选、范围、1x/2x/4x fps/分辨率、CRF、编码器、preset、音频、覆盖确认、ETA/取消；
  - 视频/HTML provider 用 `MediaServices` 导出接口；导出不走工作台缓存；
  - 清理旧 `History/Commands/CheckpointManager/SceneRenderer/CameraRenderer/VideoExporter/VideoPlayer/VideoProxy/HtmlRenderer/McpServer` 的 include 和链接；
  - 可执行名/窗口标题/安装包/MCP serverInfo 统一 `PVForge`（无 Next）；
  - 按 `USER_REQUIREMENTS.md` §9 做全量人工验收。
- 里程碑：`Ctrl+E` 导出 640×360@30 工程为 2x fps MP4，含音频、时长/帧数正确、CRF/编码器可选、ETA 更新、取消不留半成品、同名覆盖先确认；打开输出文件夹按钮可用；旧 UI 所有面板在新后端上可用；`git tag legacy-ui-baseline` 可回退。

---

## 5. 不建议做的事

1. **不要用 `NextWindow` 或 `next/*` 面板替换旧面板**。即使外观相似，也会丢掉旧交互细节，且违反“不是新壳模仿旧 UI”。
2. **不要先删旧 UI 再补**。旧 `src/app`、`src/ui` 原地迁移；删除只发生在最后清理阶段。
3. **不要给 `core::Project` 做门面后让旧 UI 原样跑**。影子模型 + 双 undo 会在 MCP 写入/外部 reload 时漂移，问题会以“偶发丢改动”的形式出现，最难查。
4. **不要保留旧 `History/Commands` 与 store undo 双栈**。一个 Ctrl+Z 只能有一个语义。
5. **不要保留旧 `McpServer/McpCommandService` 的场景授权/checkpoint**。新权限模型是“Git 决定读写 + 急停 + 写锁”。
6. **不要在旧 UI 里继续用 `SceneRenderer/CameraRenderer/VideoExporter/VideoPlayer/VideoProxy` 作为第二渲染/解码路径**。预览、MCP 预览、导出必须共用 render2 + MediaServices。
7. **不要把绝对帧 keyframe 或 `data["clipStart"]` 继续当真源**。DTO 可以给旧绘制代码看绝对帧，写回必须转相对帧。
8. **不要做旧 `.pvproj` 单文件兼容/迁移**。按需求直接提示“旧工程格式已废弃”。
9. **不要恢复语言包、主题切换、HTTP/RTP 串流、像素风改造**。这些都不在 v1 交付范围。
10. **不要让 `Next` 出现在用户可见位置**：可执行名、窗口标题、安装包、MCP serverInfo、欢迎页文案都必须是 `PVForge`。

---

## 6. 需要甲方拍板的点

1. **迁移期是否必须保留“旧目标 + 新目标”双可编译？**
   - 推荐：**不保留**。当前状态打 tag `legacy-ui-baseline`，工作分支只维护一个交付目标；需要对照旧行为时 `git worktree`/tag 切回。
   - 理由：同一份 `src/app`、`src/ui` 无法同时按旧 `Project` 和新 `Document` 编译；要双构建只能 fork UI 或大量 `#ifdef`，迁移期成本和漂移风险都高。
   - 若甲方坚持双构建：需要接受 UI 目录复制一份、两套同步修 bug 的代价。

2. **v1 是否保留旧时间轴的实体框选/多选？**
   - 现状：旧 `TimelinePanel` 有 `BoxSelect`、`m_entitySelection`；新文档写“实体第一版单选”，`USER_REQUIREMENTS.md` 也说多选/框选后续再做。
   - 推荐：**保留代码但 v1 不启用**（按新模型单选），避免和“第一版单选”冲突。
   - 若启用：需要处理多选拖动、多实体 commit、属性面板多值显示，成本增加明显。

3. **音频的秒级 trim 与淡入淡出 v1 做到什么程度？**
   - 新模型只有 `start/end` + `sourceIn`（帧）+ `volume` key；没有 `fadeIn/fadeOut` 字段。
   - 推荐：v1 只做 `trim → sourceIn` 映射，淡入淡出控件先隐藏/后续用 volume 关键帧实现；不新增模型字段。
   - 若要保留旧淡入淡出：需要扩 `AssetContent` 或 audio2 混音，属于模型变更。

4. **Git 历史面板用哪套 UI？**
   - 旧 UI 没有 Git 历史面板（旧 `History` 是 undo，不是 git）。`src/next/history/HistoryPanel` 是现成的表格面板，属于“新功能的新 UI”，不是替代旧面板。
   - 推荐：**复用 `next/history/HistoryPanel` 作为 dock，按旧深色主题调整**；不新写一套。
   - 若要求“所有面板都必须是旧风格”：需要按旧 `CollapsibleSection`/`QTreeWidget` 风格重写，成本中等。

5. **特殊内容的预览绘制统一到 render2，还是暂时保留旧 `core` helper？**
   - 旧 `ComicBox/LineDeco/BrandWall/TextBlock` 能直接画预览，但和新 `render2` 是两套画法，容易出现“属性面板预览和视口不一致”。
   - 推荐：**编辑预览统一走 render2**（用临时 `model::Entity` 渲染到 QImage），旧 helper 只在迁移过渡期用；否则特殊内容会长期双实现。
   - 若要求先保功能：可接受过渡期双实现，但必须把“预览不一致”列为已知 bug，不能当完成。
