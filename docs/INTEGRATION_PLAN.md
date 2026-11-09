# 新模型整合计划

> 第一波只做底层模块；本文定义第二波如何接入旧 app，以及哪些旧文件最终会被替换。

## 0. 开发期策略

- 旧 `pvforge` 目标暂时保留可构建，作为行为参考和回退。
- 新增 `pvforge-next` 目标，只使用 `src/model`、`src/store`、`src/solver`、`src/assets`、`src/gizmo`、`src/anim`、新渲染和新 UI。
- 新目标达到功能 parity 后，再把 `pvforge-next` 改名/替换，删除旧 `src/core/Project.*` 和旧时间轴/场景代码。
- 根 `CMakeLists.txt` 只由总控改一次；第二波 agent 不改根 CMake。
- 旧测试不强行迁移；新目标建立自己的测试入口，旧测试随旧目标一起删除。

## 进度

> 详细模块/测试/缺口见 [STATUS.md](STATUS.md)。

- 已完成：Model / Assets / Solver / Gizmo / Store / Anim / Renderer2 / Export2 / Audio2 / HTML cache / HTML2 / Video2 / MCP2 / NextCore / ProjectDocument / Renderer2 / AssetPanel / Timeline / Properties / Viewport / Settings / MCP log / NextWindow。
- 进行中：Watchdog、Store 崩溃恢复。
- 待做：导出最终 HTML/视频 provider + 高帧率对话框、崩溃恢复弹窗、视频抽图设置、端到端验收、删除旧 app。
- 根 CMake 已能一起编；`pvforge-next` 可链接。

## 当前状态

- 分支：`new-entity-model`。
- 新设计：`docs/ENTITY_MODEL.md`；状态/交接：`docs/STATUS.md`。
- 旧模型仍在 `src/core/Project.*`、`src/ui/TimelinePanel.*`、`src/mcp/*`、`src/render/SceneRenderer.*`，仅作为回退参考。
- 新模块：`src/model`、`store`、`assets`、`solver`、`anim`、`gizmo`、`render2`、`htmlcache`、`html2`、`video2`、`audio2`、`export2`、`mcp2`、`next/*`。

## 整合顺序

### 阶段 1：底层模块（进行中）

- Model / Solver / Assets / Gizmo 各自独立构建、独立测试。
- 不接旧 app，不改根 CMake。
- 完成后总控 review、修接口、合并。

### 阶段 2：Store

新增 `src/store/`，只依赖 `src/model`、`src/assets`、`src/common`。

职责：

- 打开/保存 `<name>.pvproj/` 目录。
- 扫描 `entities/<uuid>.json`，组树；写回单实体文件。
- 读写 `project.json`、markers。
- 素材导入/删除/缺失检测。
- git：自动 init、LFS 检查、每次操作 commit、revert undo/redo、历史列表。
- 事务：一次工具调用/一次 UI 操作写完后统一 commit；失败回滚。

建议 API：

```cpp
namespace pvf::store {

class ProjectStore {
public:
    static std::optional<ProjectStore> create(const QString& dir, const ProjectSettings&, QString* error);
    static std::optional<ProjectStore> open(const QString& dir, QString* error);

    const model::Project& project() const;
    model::Project& mutableProject();

    bool saveEntity(const Uuid&, QString* error);
    bool saveProject(QString* error);
    bool deleteEntity(const Uuid&, QString* error);
    bool commit(const QString& message, QString* error);

    bool undo(QString* error);
    bool redo(QString* error);
    QVector<GitCommit> history(int limit) const;

    assets::AssetStore& assets();
};

}
```

Store agent 必须写独立测试：临时目录建工程、保存/打开往返、删除实体文件、git commit/revert、素材缺失。

### 阶段 2.5：Anim（统一求值器）

新增 `src/anim/`，依赖 `model` + `solver` + `assets`。

职责：

- 给定实体和帧，统一算出 `x/y/rotation/opacity/visible`。
- 普通轨道：`model::evaluateTrack`。
- 求解段：从段 start 的初始位姿开始，调 `solver::evaluate`，取目标帧结果。
- 段结束接普通关键帧：把解算结束位姿作为隐式左值，避免跳变。
- 父级变换：父世界矩阵 × 子局部矩阵；opacity 相乘；visible/hidden/inheritVisibility 合并。
- 缓存：求解段按实体+段+帧缓存；参数/关键帧变化时失效。
- 渲染、MCP 预览、属性面板必须共用这个求值器，不能各写一套。

测试：普通关键帧、求解段中间、段结束接普通关键帧、父子变换、opacity/visible 继承、裁剪后段删除。

### 阶段 3：MCP v2

新增 `src/mcp2/`（或重写 `src/mcp/McpCommandService.*`），依赖 `store`。

- 工具注册表 + JSON schema（来自 `docs/MCP_TOOLS.md`）。
- 一次工具调用 = 一个逻辑操作 = 一个 commit。
- 批量工具：`entity.createTree/createMany/updateMany/deleteMany`、`keyframes.setMany`。
- 危险操作 `confirm: true`。
- `preview.render_frame`：
  - 复用新 renderer 渲染当前帧；
  - 返回 PNG 路径 + UI 状态 + 相机 UUID；
  - 限制分辨率/频率。
- `solver.evaluate` 调 `src/solver`，返回采样轨迹。
- `asset.import` 调 `AssetStore`；视频自动拆 WAV 并返回两个 asset hash。
- 旧 `src/mcp/*` 在新 MCP 可用后再删，不早期混改。

### 阶段 4：渲染

新增 `src/render2/` 或在旧 `SceneRenderer` 上分叉，依赖 `model` + `solver` + `assets`。

- 渲染顺序：root row 升序；父内容先画，子按 row 升序。
- 变换：父世界矩阵 × 子局部矩阵；pivot 按 MI 语义。
- 求解段：按帧取 `solver::evaluate` 结果，覆盖 x/y 或 rotation。
- 可见性：`hidden`、`visible`、`inheritVisibility`。
- 负 scale：所有实体，包括摄像机。
- 素材缺失：棋盘格 + “素材缺失”。
- HTML：复用现有 `src/html` CDP，但改为 `.pvhtml` 目录包 + 沙箱。
- 导出：选定摄像机 `[start,end)`；音频混合；MP4 管线复用。

渲染 agent 先用固定测试工程对拍：普通实体、父子、求解段、负 scale、素材缺失。

### 阶段 5：UI

- `Viewport`：接 `src/gizmo`，替换旧 gizmo 绘制/命中/拖拽；支持四角旋转、Office 对角缩放、中心四象限翻转、Alt 旋转轴心、负 scale、摄像机同一套。
- `TimelinePanel`：
  - PR 式从下往上，row 0 最底；
  - 每个父级独立 row；
  - 无限长度；
  - 求解段颜色带、起止标记、段内锁定被驱动轨道；
  - 保留私有时间线浮层、片段/关键帧模式、框选、多选。
- `PropertiesPanel`：
  - 新实体字段；
  - 求解关键帧区：效果选择、参数、轨迹预览、初速度手柄、问号说明；
  - hidden/locked/visible/physicsInteract；
  - 负 scale。
- `AssetLibraryPanel`：sha256 素材、去重、缺失占位、.pvhtml 导入、视频拆音频。
- `MainWindow`：新工程/打开目录、git 历史面板、导出选相机。

### 阶段 6：替换旧入口

- `main.cpp`：新模型测试入口、旧测试清理。
- `MainWindow`：不再使用旧 `Project` 的 scene/folder/clip 字段。
- 删除旧 `src/core/Project.*` 中不再使用的部分，或保留为兼容层直到新路径稳定。
- 旧工程 `.pvproj` 单文件：打开时提示“旧格式已废弃”。

## 第二波 agent 分工

| Agent | 目录 | 依赖 |
|---|---|---|
| Store | `src/store/**` | model, assets |
| MCP v2 | `src/mcp2/**` 或 `src/mcp/**` | store, solver |
| Renderer | `src/render2/**` | model, solver, assets, html |
| Gizmo/Viewport | `src/ui/Viewport.*` + `src/gizmo` 接缝 | gizmo, model |
| Timeline | `src/ui/TimelinePanel.*` | model, store |
| Properties/Solver UI | `src/ui/PropertiesPanel.*` | model, solver |
| App wiring | `src/app/**`, `src/main.cpp` | store, renderer, UI |

第二波开始前必须先合并第一波，冻结 `src/model` 和 `src/common/Types.h` 的公开 API。

## 风险

- **两套模型并存**：新模块不得 include 旧 `core/Project.h`；旧代码暂时保留，直到新路径可启动。
- **CMake 冲突**：根 CMake 只在第一波合并后加一次 `add_subdirectory`；第二波 agent 不改根 CMake。
- **大文件/素材**：Git LFS 硬依赖；commit 失败要显式报错。
- **求解段数值**：预览和导出必须共用 `src/solver`，不能各写一套。
- **Gizmo 与渲染变换**：两边必须共用同一个变换定义；先在 `src/gizmo` 和 `render2` 之间写对拍测试。
- **旧测试**：大量旧测试是旧模型断言，新路径稳定后统一删除/重写，不要为了过旧测试保留旧字段。
