# OBS 式模块化 UI 设计（issue #107）

> **AI 文档（非用户文档）**。设计草案，未实现。与 `docs/human/` 冲突时以人类规范为准；与代码冲突时以代码为事实并记录漂移。
>
> 状态：待人类/主 Agent 拍板。本文只做设计，不含代码改动。

## 0. 范围与非目标

**目标**：面板独立、可停靠/浮出、布局持久化；第一版只把 Workbench 从视口子控件拆成可浮出的独立窗口，验证接口后再迁 Properties / Timeline / AssetLibrary / CameraMonitor。

**非目标（第一版）**：
- 不改成 `QMainWindow` + `QDockWidget` 的全停靠体系（会改默认布局与拖拽语义）。
- 不做面板间自由拖拽重排、标签合并、多面板同屏联动。
- 不改旧 UI 的视觉、快捷键、交互语义；不浮出时与当前完全一致。
- 不动 `docs/human/`，不改代码/CMake（本任务只产出本文档）。

## 1. 现状（代码证据）

| 事实 | 位置 |
| --- | --- |
| Workbench 是 Viewport 的 `QFrame` 子控件，640×560、最小 420×300，默认隐藏、无标题栏 | `src/ui/WorkbenchPanel.*`、`MainWindow::buildSessionShell` |
| 打开方式：视口左上角 cube 按钮 → `Viewport::workbenchRequested` → `MainWindow::toggleWorkbench` → `WorkbenchPanel::showAt(anchor)` | `src/ui/Viewport.cpp`、`src/app/MainWindow.cpp` |
| 点击面板外/Esc 关闭靠 `qApp` 全局事件过滤器；锚点控件由 shell 塞给面板（`setIgnoreMouseWidget`） | `WorkbenchPanel::eventFilter`、`MainWindow` |
| CameraMonitor 已有完整的“停靠/浮出”实现：`setParent(mainWindow)` + `Qt::Window`，独立保存停靠/窗口几何，拖出主窗口自动浮出，关闭浮窗会停靠回去 | `src/ui/CameraMonitor.cpp` |
| McpLogPanel 复制了同一套逻辑（`detachWindow`/`dockWindow`/拖动/缩放/几何） | `src/ui/McpLogPanel.cpp` |
| History 已经是真正的 `QDockWidget`（可浮动/关闭），但默认隐藏 | `MainWindow::buildSessionShell` |
| Properties 在 `QScrollArea` + `QTabWidget` 里；Timeline/AssetLibrary 在嵌套 `QSplitter` 里 | `MainWindow::buildSessionShell` |
| 主窗口没有任何 `saveGeometry`/`restoreGeometry`/`saveState`；splitter 尺寸全是硬编码 | 全仓搜索无命中 |
| 设置键格式为 `<模块>/<key>`；已有 `monitor/*`、`mcpLog/*` | `src/core/Settings.cpp` |
| 单文件 1500 行硬上限 + baseline ratchet，CI 阻塞 | `tools/check-code-size.sh`、`tools/code-size-baseline.txt` |
| **并行改动**：工作树正在同时拆 `MainWindow`（`MainWindowMenus/Mcp/ProjectActions.cpp`）、`EditActions`、`mcp2/EntityTools`、`src/next/properties/PropertiesPanel`（均未提交）；`src/ui/PropertiesPanel.cpp`（4605 行）**尚未**被拆 | `git status` |

由此得出三条设计约束：

1. 不能给 Workbench 加常驻标题栏，否则默认界面变化；窗口装饰交给原生窗口边框。
2. 浮出不能改“同一个对象”这一前提；单实例 = 同一 `QWidget*` 在两种 parent 之间搬家，而不是新建窗口。
3. 新代码必须落在新文件里，不能把 `WorkbenchPanel.cpp`（1356/1500）或 `MainWindow.cpp` 顶爆；与并行拆分协调文件名。

## 2. 总体设计

三层，避免把逻辑塞回 MainWindow：

- **PanelSpec**：静态描述（id、标题、默认位置、默认快捷键、工厂函数）。纯数据，无 Qt 控件生命周期。
- **PanelHost**：每个面板一个实例，负责创建一次、显示/隐藏、停靠/浮出、几何读写、快捷键 QAction、屏幕 clamp、可见性/浮出状态信号。**不拥有**面板（不 `delete`）。
- **PanelSlot**：面板“家”的抽象：视口浮层、Splitter 子项、Tab 页、QDockWidget。负责“浮出时腾出位置、停靠时放回原位”，并保存 splitter sizes / tab index。
- **PanelManager**：持有全部 host、QAction 表、布局恢复/重置，并把屏幕增删事件分发给 host。v1 可先由 `MainWindowPanels.cpp` 承担，接口按 manager 设计，避免以后再搬。

`MainWindow` 只做三件事：建 slot、把 session 交给 host、接线（信号转发）。**面板内部逻辑不回到 MainWindow**。

```
MainWindow ── PanelManager ── PanelHost ── PanelSlot ── 具体 QWidget
                  │               │
                  └── QAction 表   └── PanelSettings(QSettings)
```

为什么用组合而不是让所有面板继承一个基类：
- Workbench 是 `QFrame`（自带边框/阴影），CameraMonitor 是自绘 `QWidget`，Properties 需要外层滚动容器，基类会强迫改继承链。
- 组合可以逐步接入，不迁移的面板完全不受影响，单步可回退。

## 3. 面板接口

### 3.1 `PanelSpec` 字段

| 字段 | 语义 |
| --- | --- |
| `id` | 稳定字符串，用于设置键、菜单 objectName、测试查询：`workbench`、`properties`、`timeline`、`assetLibrary`、`cameraMonitor`、`mcpLog`、`perfMonitor`、`projectProperties`、`environment`、`history` |
| `titleKey` / `fallbackTitle` | `Lang::t` 键 + 中文兜底（`panel.workbench` = 工作台） |
| `defaultPlacement` | `ViewportOverlay` / `SplitterChild` / `TabPage` / `MainDock` / `IndependentWindow` |
| `defaultVisible` | 打开工程时是否显示；Workbench/MCP log/History = false，CameraMonitor 读旧设置 |
| `defaultShortcut` | 见 3.4；无默认则留空 |
| `factory` | `EditorSession* → QWidget*`，只在首次 show 时调用一次；parent 由 slot 决定 |
| `resizable` / `minSize` | 面板允许的尺寸约束（浮出时用） |
| `settingsPrefix` | 默认 `panel/<id>`；CameraMonitor/McpLog 迁移期可指向旧前缀 |

### 3.2 生命周期与可见性状态机

每个面板只有三个稳定状态：`Hidden` / `Docked` / `Floating`。“可见”与“浮出”正交：

- `show()`：从 Hidden 进入上次的 mode（`Docked` 或 `Floating`）。
- `hide()`：保存几何与 visible=false，保留 mode，不销毁。
- `toggle()`：
  - 浮出且可见 → `raise()` + `activateWindow()`，不隐藏（避免点视口 cube 时误关窗口）。
  - 停靠且可见 → 隐藏（保留旧行为）。
  - 隐藏 → 按上次 mode 显示。
- `toggleFloating()`：`Docked ↔ Floating`，保持可见；不可浮出的面板禁用该动作。
- 首次 `show()` 才调 factory，之后永远复用同一个指针。单实例断言：`findChildren<WorkbenchPanel*>().size() == 1`。

### 3.3 session/document 获取

- 构造时注入 `EditorSession*`，禁止单例/全局查找。当前项目是“一窗口一 session”，`showSessionWindow` 换工程即换 `MainWindow`，因此面板可安全缓存 `session`。
- Document 一律通过 `session->document()` 取；不要缓存 `next::Document*` 跨工程（本设计不跨）。
- **允许**：面板只读监听 `Document::changed/selectionChanged/playheadChanged`，以及 `EditorSession::playback()`。
- **禁止**：面板持有其他面板的 `QWidget*`、直接调用其他面板的方法、直接改别人的控件。
- **写入**：只走 `EditorSession::editActions()` / `Document::commit*`；跨面板副作用（如“创建后刷新素材队列”）发 intent 信号，由 shell 转给对应面板。
- **shell → 面板**：显式 setter（`setCurrentFrame`、`setSelectedUuid`、`refresh`）；**面板 → shell**：信号。

需要修的现存越界（按迁移步骤逐个处理，不阻塞 v1）：

| 越界 | 现状 | 目标 |
| --- | --- | --- |
| shell 抓 Viewport 的按钮给 Workbench | `setIgnoreMouseWidget(m_viewport->workbenchButton())` | host 提供“锚点点击不算外部点击”的谓词/区域；Workbench 不知道 Viewport |
| 面板直连面板 | `Viewport::viewChanged → CameraMonitor::updateFromViewport` | 由 shell 转发或改为“布局变化”信号 |
| shell 操作 Tab 细节 | selectionChanged 里改 tab 文案、切 tab | 交给 TabPage slot / Properties 面板自己处理标题 |
| shell 计算浮窗摆位 | `arrangeMcpLog()` 读 monitor 几何 | 布局协调归 PanelManager；v1 不动，v4 再收 |

### 3.4 快捷键

- 每个面板一个 checkable `QAction`，挂在 MainWindow；同一 action 同时 `addAction` 到浮出窗口，保证窗口有焦点时仍能切换。**不要**改成 `ApplicationShortcut`（会在设置对话框/文本框里误触发）。
- 浮出窗口内：只有 ApplicationShortcut（当前是 Space）全局有效；`Left/Right/Home/End/F` 等 WindowShortcut 不再生效——这是原生窗口的预期行为，也是面板内文本框/SpinBox 需要的行为。
- 默认键位（均未被占用，已对全仓 `setShortcut` 扫描）：

| 面板 | 快捷键 |
| --- | --- |
| 工作台显示/隐藏 | `Ctrl+Shift+W` |
| 属性 | `Ctrl+Shift+P` |
| 时间轴 | `Ctrl+Shift+T` |
| 素材库 | `Ctrl+Shift+A` |
| 摄像机监视器 | `Ctrl+Shift+M` |
| MCP 日志 | `Ctrl+Shift+L` |
| 性能监视 | `Ctrl+Shift+F` |

- “浮出/停靠切换”不给默认快捷键，避免误操作把窗口弹飞；只放菜单和面板内按钮。
- 启动时/测试中扫描所有 QAction，重复键序列视为失败（Qt 的 ambiguous shortcut 会静默吞键）。

## 4. 工作台浮出

### 4.1 对象与父子关系

- 始终是**同一个** `WorkbenchPanel` 实例。
- 停靠：`parent = Viewport`，`Qt::Widget`，几何是视口坐标。
- 浮出：`parent = MainWindow`，`Qt::Window`，几何是屏幕坐标；MainWindow 销毁时随之销毁，不会活过 session。
- 切换顺序：`hide()` → `setParent(...)` → 设置 window flags → 恢复几何 → `show()`。先 hide 避免原生窗口重建时的闪烁与焦点竞态（CameraMonitor 现在不 hide 直接改，v4 一并收拾）。
- 不做 frameless：用原生标题栏提供拖动/关闭/最小化，零自绘成本。

### 4.2 停靠几何

沿用现算法，不改变视觉：

- 锚点 = `viewport->workbenchButton()->geometry().bottomLeft() + (0,8)`（视口坐标）。
- 尺寸 = `min(640, max(420, 视口宽-16)) × min(560, max(300, 视口高-16))`。
- 位置 clamp 到 `[8, 视口尺寸 - 面板尺寸 - 8]`；保留 160ms 位移动画。
- 改进（建议随 v1 做）：停靠最小尺寸降到 `360×240`，内部已有滚动区；避免 1000×620 窗口下“创建”按钮被裁（`UI_COMPARISON_V020.md` 的 #6）。浮出最小尺寸仍为 `420×300`。

### 4.3 浮出几何

- 默认 `640×560`；放在视口左上角屏幕坐标 `+ (48,48)`，clamp 到屏幕可用区。
- 记住上次浮出几何；若不在任何屏幕上（拔掉副屏/分辨率变化），移到主屏居中。
- `moveEvent`/`resizeEvent` 用 500ms 防抖写设置（原生拖拽没有 mouseRelease，不能只靠关闭时保存）。
- 浮出时：关闭阴影（`QGraphicsDropShadowEffect` 在顶层窗口会被裁掉/不绘制）、去掉 `QFrame::StyledPanel` 内边框和样式表边框，避免“窗口边框套边框”；停靠时恢复。

### 4.4 焦点、Esc、关闭

| 场景 | 停靠 | 浮出 |
| --- | --- | --- |
| 打开 | 不抢焦点（现状） | `show/raise/activateWindow`，由用户点击输入框 |
| Esc | 关闭面板（现状，`qApp` 过滤器） | 关闭窗口（面板级 `keyPressEvent`，不装全局过滤器） |
| 点击面板外 | 关闭（现状） | **不关闭**（OBS 语义）；点视口 cube 只 raise/activate |
| 标题栏 × | 无（无标题栏） | 隐藏，记 visible=false，保留浮出模式与几何 |
| 关闭后重开 | 回到停靠原位 | 按上次浮出几何重新显示 |
| 隐藏/停靠后 | `viewport->setFocus()` | 同上 |

- `qApp->installEventFilter` 只在停靠可见期间安装；浮出前移除，否则点主窗口任何地方都会把浮窗关掉。
- 关闭不销毁、不断 session；`closeEvent` 只保存并 hide。
- 单实例：再次打开只 `raise/activate`；不新建。

### 4.5 浮出入口（重要取舍）

默认界面不能变，所以**不加常驻标题栏**。入口：

1. 视图菜单：`工作台`（checkable，`Ctrl+Shift+W`）+ `工作台浮出/停靠`（checkable）。
2. 现有底部操作行（“创建”按钮旁）加一个小按钮，文案在“浮出/停靠”间切换，objectName `workbenchFloatButton`。它只在面板已经打开时可见，不新增行、不移动现有控件。
3. 浮出后可用原生标题栏拖到副屏；停靠用菜单/按钮/快捷键。

**明确取舍**：不做“从停靠状态直接拖出”的手势。Workbench 没有可拖拽的空白头部，给它加头部会改变默认 UI，违反约束。若人类要求必须支持直接拖出，需要先批准加一条 20px 头部栏（这是一个独立的 UI 变更）。

## 5. 布局持久化

### 5.1 QSettings 键

| 键 | 类型 | 默认 | 说明 |
| --- | --- | --- | --- |
| `panel/<id>/visible` | bool | 见 spec | 当前是否显示 |
| `panel/<id>/mode` | string | `docked` | `docked` / `floating` |
| `panel/<id>/dockGeometry` | QRect | 由锚点算 | 停靠/浮层坐标；`(-1,-1,…)` 表示未设置（沿用旧约定） |
| `panel/<id>/windowGeometry` | QRect | 640×560 或居中 | 浮出窗口几何 |
| `panel/<id>/alwaysOnTop` | bool | false | 仅 Monitor/MCP log 有意义 |
| `layout/mainSplit/sizes` | int list | `{640,220}` | 竖向主分割 |
| `layout/timelineAssetSplit/sizes` | int list | `{180,1000}` | 时间轴左侧素材库 |
| `layout/topSplit/sizes` | int list | `{1100,320}` | 视口/右侧标签 |
| `layout/rightTabs/index` | int | 0 | 右侧标签页 |
| `ui/mainWindow/geometry` | QRect | 无（保持 1440×900） | 可选；v1 不做 |

- 兼容旧键：CameraMonitor 先读 `panel/cameraMonitor/*`，缺失则回退 `monitor/*`；McpLog 同理回退 `mcpLog/*`；写入只写新键，一次迁移后旧键自然废弃。不要删除旧 getter，避免测试/设置页回归。
- 所有写入走 `PanelSettings`，不在面板内部散落 `QSettings`。

### 5.2 默认布局（无任何键时必须逐像素等于现在）

- Workbench：隐藏，停靠在视口，几何由锚点算。
- CameraMonitor：按 `monitor/visible`（默认 true），停靠视口右下 16px。
- MCP 日志：隐藏。
- History dock：隐藏。
- 右侧 tab：项目属性在前，属性页在后。
- splitter：`mainSplit {640,220}`、`timelineAssetSplit {180,1000}`、`topSplit {1100,320}`。
- 窗口：1440×900，最小 1000×620。

### 5.3 恢复时机

1. PanelHost 构造时只读设置到字段，不碰控件几何（此时 viewport 尺寸还不对）。
2. 默认隐藏的 Workbench 不在启动时恢复；首次 show 时再 clamp。
3. 若持久化 `visible=true` 的浮层/浮窗：在 `MainWindow::showEvent` 之后（或 `QTimer::singleShot(0)`）统一恢复，保证父窗口尺寸与屏幕信息已就绪。
4. splitter 尺寸在主窗口构造时恢复；记录点在关闭窗口/拖动结束时。

### 5.4 屏幕变化 clamp

纯函数 `clampToScreens(desired, minimum, screens, primary)`：

1. 尺寸先 `max(minimum)`，再不超过目标屏可用区（留出窗口边框余量）。
2. 优先选与 desired 相交面积最大的屏；都不相交 → 主屏居中。
3. 至少有 120×60 的标题栏区域留在屏内；优先保留左上角。
4. 停靠浮层额外 `clampToParent`：限制在父控件矩形内；父控件小于最小尺寸时保留最小尺寸、左上对齐并允许溢出（现状），或按 4.2 的较低停靠最小值处理。
5. 监听 `QGuiApplication::screenRemoved`、`screenAdded`、`QScreen::availableGeometryChanged`，对全部 Floating host 重新 clamp；DPI 变化同样处理。
6. 全屏：当前没有全屏入口，不实现特殊路径；将来加全屏时浮窗保持独立顶层即可。

### 5.5 重置布局

“视图 → 重置面板布局”：全部面板停靠、浮层隐藏、splitter 回默认、清 `panel/*` 与 `layout/*`。只重置，不重建窗口；实现为对每个 host 调 `dock()+hide()` 再设 splitter sizes。第一版可不做，但键设计要允许。

## 6. 迁移顺序（每步独立可回退）

| 步骤 | 内容 | 改动面 | 回退方式 |
| --- | --- | --- | --- |
| 0 | 拆 `WorkbenchPanel.cpp` 的 session 创建页（为加 host 代码腾行数）；新增 PanelHost/PanelSettings/PanelSlot 骨架 + 纯几何 clamp 测试 | 新文件 + Workbench 拆分 | 纯新增，revert 即可 |
| 1 | Workbench 浮出/停靠 + 菜单/快捷键 + 几何持久化 + 自检 | `WorkbenchPanel`、`MainWindowPanels.cpp`、`MainWindowMenus.cpp`、CMake、测试 | 恢复 `toggleWorkbench()` 旧实现，删 host 接线 |
| 2 | Properties：新增 `PropertiesDock`（QScrollArea + PropertiesPanel），TabPage slot；浮出时 `setTabVisible(false)`，停靠恢复索引 | `MainWindow`、新 wrapper | 把 `m_objectTab` 换回原构造 |
| 3 | Timeline + AssetLibrary：SplitterChild slot；浮出 = `hide()` 让 splitter 收起，停靠 = 恢复 sizes；拖拽/播放/选中照旧 | `MainWindow`、slot | 恢复直接 addWidget/硬编码 sizes |
| 4 | CameraMonitor + McpLog：把各自私有的 detach/dock/拖动/几何迁到 PanelHost；保留现有外观与按钮；旧设置键回退读取 | 两个面板、Settings | 恢复各自 `detachWindow/dockWindow` |
| 5 | History（已是 QDockWidget，只接 host）、ProjectProperties/Environment、重置布局、主窗口几何 | 可选 | 单独 commit |

顺序理由：

- Workbench 是自包含浮层，只依赖 session，不与 splitter/标签耦合，风险最低，正好验证接口。
- Properties 是 Tab 页，需要 slot 抽象；先验证“浮出后 tab 位置不丢”。
- Timeline/AssetLibrary 涉及 splitter sizes、拖放、播放联动，交互风险最高，放后面。
- CameraMonitor 已经有成熟实现，迁移是去重而不是造新行为；最后做，避免一开始就动已有可用功能。

## 7. 文件拆分（≤1500 行）

CI 规则：新文件 >1500 行直接判红；历史大文件只能降不能升（`tools/code-size-baseline.txt`）。因此：**新增代码一律进新文件；不抬高 baseline**。

> 并行状态：其他 agent 正在拆 `MainWindow.cpp`（`MainWindowMenus/Mcp/ProjectActions.cpp`）、`EditActions`、`mcp2/EntityTools` 与 `src/next/properties/PropertiesPanel.cpp`，CMake 也已被修改（均未提交）。本计划的新文件避开这些名字；`src/ui/PropertiesPanel.cpp`（4605，实际交付壳使用）仍需单独拆。行数会随并行拆分继续变化，以开工时的 `wc -l` 和 `tools/check-code-size.sh` 为准。

| 当前文件 | 行数 | 拆分建议 |
| --- | --- | --- |
| `src/ui/WorkbenchPanel.cpp` | 1356 | `WorkbenchPanel.cpp`（构造/buildUi/show/hide/事件/分派 ~350）；`WorkbenchMediaPage.cpp`（媒体页 ~220）；`WorkbenchTextPage.cpp`（文字页 ~160）；`WorkbenchVectorPage.cpp`（矢量页 ~130）；`WorkbenchSpecialPage.cpp`（特殊页/漫画/径向/半调 ~380）；`WorkbenchBrandWallPage.cpp`（品牌墙 ~260）；`WorkbenchSessionCreate.cpp`（session 创建实体 ~320）。同一 class 拆多个 TU，成员仍在头文件，避免 PIMPL 大改；匿名 namespace 的公共小工具提到 `WorkbenchPanelInternal.h`。 |
| `src/ui/PropertiesPanel.cpp` | 4605 | `ui/properties/PropertiesPanel.cpp`（构造/刷新/选中 ~500）；`PropertiesPanelTransform.cpp`；`PropertiesPanelContent.cpp`；`PropertiesPanelSolver.cpp`；`PropertiesPanelKeyframes.cpp`；`PropertiesPanelSession.cpp`；共享控件指针进 `PropertiesPanelPrivate.h`（或 PIMPL）。要另开 issue，不和 #107 混。 |
| `src/ui/TimelinePanel.cpp` | 4473 | `TimelinePanel.cpp`（几何/滚动/刷新 ~900）；`TimelinePaint.cpp`；`TimelineInput.cpp`（鼠标/滚轮/键盘/焦点）；`TimelineKeyframes.cpp`；`TimelineClips.cpp`（拖放/右键菜单/popup）；`TimelineSession.cpp`（EditActions 提交）。共享私有结构进 `TimelinePanelInternal.h`。 |
| `src/ui/AssetLibraryPanel.cpp` | 2000 | `AssetLibraryPanel.cpp`（UI ~550）；`AssetLibrarySession.cpp`（条目/过滤/预览 ~550）；`AssetLibraryImport.cpp`（导入/重命名/标签/删除/重导 ~550）；`AssetLibraryProgress.cpp`（视频抽帧队列 ~300）。 |
| `src/ui/Viewport.cpp` | 2748 | 不在 #107 范围；如后续要为面板让路，按 `ViewportSession`/`ViewportGizmo`/`ViewportCamera`/`ViewportPaint` 拆。 |
| `src/ui/CameraMonitor.cpp` | 913 | 保持；迁 host 后删掉私有 detach/dock/几何代码，应更小。 |
| `src/app/MainWindow.cpp` | ~900（并行拆分中） | 新逻辑进 `MainWindowPanels.cpp`（host 建表/接线 ~350）、菜单进 `MainWindowMenus.cpp`；不要回涨。 |

Workbench 的 1356 行离上限只有 144 行，直接加浮出逻辑会顶线，所以步骤 0 必须先拆。

## 8. 风险

标 **P0** 的是必须在前两步解决的问题。

### P0-1 重新 parent 引发的焦点/快捷键/原生窗口问题

`setParent` + `Qt::Window` 会重建原生窗口：焦点丢失、`QAction` 的 WindowShortcut 上下文切换、窗口管理器可能调整几何/装饰，阴影和 QFrame 边框会串味。当前 CameraMonitor 在不 hide 的情况下直接改 parent，说明现有实现是靠“能跑”而非严谨。

缓解：统一走 PanelHost 单一路径；切换前 `hide()`；浮出时移除阴影/边框；同一 QAction 同时挂主窗口与浮窗；停靠/隐藏后显式把焦点还给视口；测试只断言 `geometry()` 和 parent/flags，不断言 WM frame。

### P0-2 父对象、析构与 AI 锁

面板现在分别归 viewport / splitter / tab 所有；浮出后 parent 变成 MainWindow。MainWindow 析构顺序、`setHumanEditingEnabled()` 的裸指针列表、测试里的 `findChild` 与 `isEnabled` 断言都可能踩空；浮出的面板不在原布局里，也容易被“刷新/锁定”逻辑漏掉。

缓解：PanelHost 只持 `QPointer`，不删控件；浮出 parent 设为 MainWindow（`Qt::Window`），保证随 session 销毁；AI 锁改为遍历 host 列表而不是散落指针；任何 refresh/lock 都由 host 统一转发；测试覆盖“浮出时 AI 写锁定仍生效”。

### P0-3 多屏/几何持久化与测试环境差异

浮窗几何依赖 WM；offscreen 平台与真实 X11/Wayland 行为不同；拔屏、缩放、任务栏变化都会让保存的 QRect 失效。若恢复逻辑在窗口 show 之前跑，父控件尺寸还是默认值，会 clamp 到错误位置。

缓解：几何用纯函数 clamp（可单测）；恢复延迟到首帧 show 之后；浮窗移动/缩放用防抖保存；恢复失败一律回落主屏居中；测试容忍少量边框误差，不做像素/截图断言。

### 其他风险

| 风险 | 说明 | 缓解 |
| --- | --- | --- |
| 视口鼠标事件 | 停靠时 Workbench 依赖全局过滤器“点外面关闭”；浮出时若不移除，点主窗口就关窗 | 浮出前 `removeEventFilter`；浮出状态不装全局过滤器；锚点判定改由 host 提供 |
| 快捷键冲突 | 时间轴自己处理 `Left/Right/Delete/S/M`，MainWindow 又注册了 WindowShortcut；浮窗内行为会变 | 建立快捷键表 + 启动扫描重复；浮窗内明确只保留 ApplicationShortcut（Space）；面板内文本框的 Ctrl+Z 保持文本撤销 |
| 浮出后“编辑/求解/缓存/播放”不一致 | Workbench 只依赖 session，不依赖 viewport，理论上安全；但 `refreshSession` 由 shell 主动推，漏推就显示旧数据 | host 在 show 时调用面板 `aboutToShow/refresh`；保留 shell 的 `refreshSessionAfterExternalChange → workbench->refreshSession()` |
| 直接拖出 | 默认界面不能加标题栏，停靠态没有可拖区域 | v1 用菜单/按钮/快捷键浮出，再拖原生标题栏；若人类坚持直接拖出，先批准 20px 头部栏 |
| 并行重构冲突 | `MainWindow`、`EditActions`、`mcp2`、`next/properties` 正在被其他 agent 改；新建文件若撞名会互相覆盖 | 本计划只新增 `ui/panels/*` 与 `MainWindowPanels.cpp`；开工前先 rebase/确认未提交文件状态；Workbench 拆分放在独立 commit |
| 测试假绿 | 只断言 `isVisible()` 无法发现“窗口在屏幕外/被裁剪/焦点丢失” | 断言 parent、`isWindow()`、`geometry().intersects(screen)`、单实例计数、关闭后对象存活、session 仍可提交 |
| 性能 | 浮窗每帧重绘/预览与主窗口叠加 | Workbench 是低频面板；CameraMonitor 已有预览缓存。不为浮窗加额外定时器；移动保存防抖 500ms |

## 9. 最小第一版实现清单（文件级）

第一版只实现 Workbench 的浮出/停靠，**不迁移其他面板**。

### 新增文件

1. `src/ui/panels/PanelId.h` — 面板 id 常量（v1 只用 `workbench`）与标题键约定。
2. `src/ui/panels/PanelSpec.h` — 描述结构（id/title/defaultPlacement/defaultVisible/shortcut/factory/minSize/settingsPrefix）。
3. `src/ui/panels/PanelGeometry.h/.cpp` — 纯函数：屏幕选择、浮窗 clamp、父控件 clamp、屏幕移除后的重定位；不依赖控件，便于单测。
4. `src/ui/panels/PanelSettings.h/.cpp` — QSettings 读写（`panel/<id>/*`、`layout/*`）、旧键回退、默认值集中管理。
5. `src/ui/panels/PanelHost.h/.cpp` — 创建一次、show/hide/toggle/toggleFloating、parent/flags 切换、几何读写、可见/浮出信号、关闭处理、QAction 管理、防抖保存。
6. `src/ui/panels/PanelSlot.h` + `OverlaySlot.h/.cpp` — v1 只需要“视口浮层”slot；Splitter/Tab slot 留接口不实现。
7. `src/app/MainWindowPanels.cpp` — `buildPanelHosts()`、Workbench host 接线、`toggleWorkbench()` 改调 host、启动后的延迟恢复。`MainWindow.cpp` 只保留调用。
8. `src/tests/PanelLayoutTests.cpp` — 注册 `--panel-layout-test`；覆盖 9.2 的用例。

### 修改文件

8. `src/ui/WorkbenchPanel.h/.cpp` — 增加 `aboutToShow()`、`setFloatingChrome(bool)`、`showFloating()`/`dockBack()` 的呈现钩子（几何由 host 决定）；浮出态用面板级 Esc 代替全局过滤器；阴影/边框切换。为控制行数，先做步骤 0 的拆分（新增 `WorkbenchSessionCreate.cpp` 等）。
9. `src/app/MainWindow.h` — 增加 `PanelHost* m_workbenchHost`（保留 `m_workbench` 供测试/锁遍历），声明 `buildPanelHosts()`、`restorePanelLayout()`。
10. `src/app/MainWindow.cpp` — `buildSessionShell()` 中把 `new WorkbenchPanel(...)` + `setIgnoreMouseWidget` + `showAt` 改为 host 创建；`toggleWorkbench()` 委托；`setHumanEditingEnabled()` 遍历 host；`refreshSessionAfterExternalChange()` 保持对 Workbench 的刷新。
11. `src/app/MainWindowMenus.cpp` — 视图菜单加 `workbenchAction`（checkable，`Ctrl+Shift+W`）与 `workbenchFloatAction`（checkable，“浮出工作台”/“停靠工作台”）；状态由 host 信号回填，屏蔽递归。
12. `src/ui/WorkbenchPanel.cpp`（若不做完整拆分，最小改动版）— 底部操作行加 `workbenchFloatButton`（仅面板打开时可见）。这是 v1 唯一的视觉增量，需人类确认；不批准则只保留菜单/快捷键。
13. `CMakeLists.txt` — 加入 6 个新 .cpp（PanelGeometry/PanelSettings/PanelHost/OverlaySlot/MainWindowPanels/PanelLayoutTests）与拆分出的 Workbench TU。
14. `src/tests/TestRegistry.h/.cpp` — 注册 `registerPanelTests()` / `--panel-layout-test`。
15. `assets/lang/zh_CN.json` / `en_US.json` — `panel.workbench`、`action.floatWorkbench`、`action.dockWorkbench`。
16. `docs/AI/OBS_UI_PLAN.md` — 本文；实现后追加“实现状态/偏差”。

### 9.1 验收标准

- 无任何 `panel/*` 设置时，启动画面与现在一致；Workbench 仍默认隐藏，打开位置/尺寸/点外关闭/Esc 不变。
- `Ctrl+Shift+W` 打开；再按隐藏；浮出后按只 raise，不隐藏。
- “浮出”后成为独立原生窗口，可拖到副屏；关闭只隐藏；重开恢复浮出几何与位置。
- “停靠”后回到视口原算法位置；几何持久化跨进程重启（测试用临时 QSettings）。
- 单实例：反复打开/浮出/停靠后 `findChildren<WorkbenchPanel*>()` 恒为 1，指针不变。
- 浮出期间：创建实体、品牌墙、素材选择、自动 key、播放、撤销/重做、AI 写锁定与停靠一致；`--mcp-lock-test` 仍通过。
- `bash tools/check-code-size.sh` 通过；所有新增文件 <1500 行；baseline 不抬高。
- `--panel-layout-test` 通过；已有 `--camera-monitor-test`、`--mcp-lock-test`、`--inkscape-handles-test` 不回归。

### 9.2 测试用例（`--panel-layout-test`，offscreen）

1. 初始：Workbench 不可见、parent 是 Viewport、`isWindow()==false`。
2. 打开：可见、停靠几何在视口内、`objectName` 不变。
3. 浮出：`isWindow()==true`、parent 是 MainWindow、`geometry()` 与某个屏幕可用区相交、最小尺寸满足。
4. 再次 `toggle`：只 raise，`isVisible()` 仍为 true。
5. 隐藏→重开：恢复浮出模式与几何（允许 ±2px）。
6. 停靠：parent 回 Viewport、几何在视口内、模式=docked。
7. 关闭浮窗：对象仍存活（QPointer 非空）、session/document 仍可提交、visible=false。
8. 单实例：多次 show/hide/float/dock 后计数为 1。
9. 几何 clamp 纯函数：完全在屏外、部分出界、主屏/副屏切换、屏移除、小于最小尺寸。
10. 持久化：写入后新 host 读取一致；旧键 `monitor/*` 回退（v4 前只测 helper）。
11. AI 锁：模拟写锁定后 Workbench `isEnabled()==false`，浮出/停靠不改变该状态。

### 9.3 手工验证矩阵

- 单屏 1920×1080、1000×620 最小窗口。
- 双屏（副屏在左/右、不同 DPI）：浮出拖到副屏 → 重启恢复；运行中拔掉副屏 → 自动回主屏。
- 浮出窗口获得焦点时：Space 播放、输入框 Ctrl+Z 是文本撤销、Left/Right 在 SpinBox 内正常步进。
- 停靠态：点视口 cube 关闭、Esc 关闭、点主窗口其他区域关闭（现状）。
- 浮出态：点主窗口不关闭；点 cube 只 raise；关闭按钮隐藏；菜单/快捷键重开。
- 中文/英文语言切换后菜单与标题正确。

## 10. 与 issue #107 验收对照

| issue 要求 | 本设计 |
| --- | --- |
| 每个面板独立模块、统一接口 | `PanelSpec` + `PanelHost` + `PanelSlot`；v1 只接 Workbench |
| 面板间只通过 session/document 信号 | 3.3 明确允许/禁止；列出 4 处现存越界并给迁移步骤 |
| 主窗口只装配 | 新逻辑进 `MainWindowPanels.cpp`，`MainWindow.cpp` 不增长 |
| 默认布局不变 | 5.2 逐项锁定；v1 唯一视觉增量是面板内“浮出”按钮（待批准） |
| 可拖出为 QWidget(Qt::Window)、停靠回去 | 4.1–4.3；直接拖出手势受“不加标题栏”约束，v1 用菜单/按钮 + 原生标题栏 |
| 关闭/重开保持几何、单实例、不销毁 session | 4.4、4.3、9.2 用例 5/7/8 |
| 工作台浮出后编辑/求解/缓存/播放/快捷键一致 | Workbench 只依赖 session；9.1 验收；风险表“刷新漏推” |
| 每面板 ≤1500 行 | 第 7 节拆分表 + CI ratchet |
| 面板接口有测试 | 9.2 `--panel-layout-test` |
