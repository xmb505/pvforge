# 旧 UI 功能盘点（删除前评估）

> 日期：2026-11-10  
> 范围：`src/app/*`（旧 MainWindow / 各对话框）、`src/ui/*`（旧面板与视图）、以及它们依赖的 `src/core`、`src/media`、`src/audio`、`src/html` 实现。只读评估，未改代码。

## 结论

旧 UI 约 2.2 万行，功能面远宽于新壳。新壳已经把新模型的主链路接起来了：实体/行/裁剪/关键帧/合并菱形/私有时间线、素材哈希与缺失、求解段、HTML 缓存、MP4 导出、MCP 日志、设置和欢迎页都有对应实现；主窗口、属性面板、MCP 日志、欢迎页可以按“已覆盖”处理，素材替换和导出精细控制仍需补。真正需要人类拍板的，是旧壳里三类新模型没有直接对应的能力：特殊生成内容（漫画框/线条装饰/品牌墙/点阵）、相机监视器（临摹参考视频与 HTTP/RTP 串流），以及精细控制（完整文字排版、30 种缓动预设、CRF/编码器、导出预览）。场景模型按设计删除；曲线图编辑器和语言包在旧壳里就已经是隐藏/半成品，不建议迁移。

## 逐项清单

| 功能 | 旧文件 | 新壳现状 | 建议 | 理由 |
|---|---|---|---|---|
| 主窗口布局 / 菜单 / 状态栏 | `src/app/MainWindow.cpp/.h` | `src/next/app/NextWindow.cpp` 的 `buildUi` / `buildEditor` / `updateStatus`：左素材箱、中视口、右属性、下时间轴；文件/编辑/实体/播放/视图菜单；工具栏播放头、摄像机、Gizmo、自动关键帧；状态栏工程路径、提交状态、AI 状态、MCP 状态、帧号、fps。 | 已覆盖 | 新壳布局和状态栏信息比旧壳更完整。旧壳的 Help/About、素材打包菜单、保存动作没有搬，其中保存已由自动 commit 取代，属于设计变化。 |
| 旧时间轴（clip / 合并 / 缩放 / 私有时间线 / 吸附） | `src/ui/TimelinePanel.cpp/.h` | `src/next/timeline/TimelineWidget.*`：PR 式从下往上、拖动/裁剪（含子树递归）、合并菱形多选拖动与复制粘贴删除、Ctrl+滚轮缩放、拖素材建实体、私有时间线浮层、求解段色带、标记绘制；实体仍是单选，无框选/多选。 | 已覆盖 | 新模型要求的 clip、关键帧、私有时间线、合并菱形都已实现；实体多选/框选在 `ENTITY_MODEL.md` 中明确延后。旧时间轴本来没有吸附，吸附属于视口设置（见 Gizmo 一行）。 |
| 旧时间轴场景 / 标记（注释） | `src/ui/TimelinePanel.h`（scene/annotation 访问器全为空实现）、`src/app/MainWindow.cpp` 的 `SceneDialog` / `AnnotationDialog`、`src/core/Project.h` 的 `Scene` / `Annotation` | `TimelineWidget` 只绘制 `project.markers`，没有增删改入口；`Scene` 概念已从新模型删除；MCP 有 `markers.set/remove`。 | 需要迁移 | 场景按设计删除，不需要救。旧注释对话框虽然有代码，但没有任何菜单/右键入口，时间轴也不画，属于死 UI；新时间轴能显示标记但不能编辑，需补标记 CRUD（标题、颜色、备注、拖动）。 |
| 旧属性面板（变换 / 缓动 / 关键帧 / 曲线编辑器） | `src/ui/PropertiesPanel.cpp/.h`、`src/ui/CurveEditor.cpp/.h` | `src/next/properties/PropertiesPanel.*`：基础、变换、每轨菱形关键帧、缓动下拉、自定义贝塞尔、内容页、求解段卡片。旧 `CurveEditor` 在当前旧壳已被 `hide()`，只保留信号。 | 已覆盖 | 新面板覆盖了变换、关键帧、auto-key、缓动和求解段。曲线图编辑器在旧壳就不可见，新模型也没有要求图编辑器，不必复活。 |
| 旧缓动预设库（30 种曲线） | `src/ui/EasingPicker.cpp/.h`、`src/ui/EasingChooser.cpp`、`src/core/Easing.cpp/.h` | `src/model/Easing.h` 只支持 `linear / easeIn / easeOut / easeInOut / bezier / instant / step`；Properties 下拉只有 6 项；旧 quad/cubic/quart/quint/sine/expo/circ/elastic/back/bounce × 入/出/入出共 30 种未迁移。 | 需要迁移 | 预设是 PV 动效手感的核心。旧 `EasingPicker` 和 `Easing` 求值是自包含的，可整体移植，只需扩充 `model::EasingSpec` 的类型与下拉项。 |
| 旧素材库（导入 / 预览 / 替换 / 缺失） | `src/ui/AssetLibraryPanel.cpp/.h`、`src/ui/PropertiesPanel.cpp` 素材区、`MainWindow::replaceSourceFromDialog` | `src/next/assets/AssetPanel.*`：内容哈希列表、搜索、标签筛选、导入文件与 `.pvhtml`、改名/标签/删除、缺失标记、拖到时间轴、视频抽帧进度。Properties 素材页只读显示 hash/type/sourceIn/volume，没有替换选择器。 | 需要迁移 | 列表、导入、缺失、标签已覆盖；给已有实体换素材目前只能走 MCP `asset.assign`，缺一个旧式“更换素材”按钮和悬停大图预览。 |
| 旧相机监视器（浮窗 / 缩放） | `src/ui/CameraMonitor.cpp/.h` | 无独立监视器。`src/next/viewport/ViewportWidget.*` 有摄像机下拉和取景框，主视口可缩放/平移。 | 需要迁移 | 主视口能替代基本预览，但无法边编辑边看成品。临摹参考视频（透明度叠加）和 HTTP/RTP 串流是旧壳独有能力；只做浮窗预览成本低，连临摹/串流一起搬成本中等。 |
| 旧导出对话框 / 进度（编码参数 / 音频 / 范围） | `src/app/ExportDialog.cpp/.h`、`src/app/ExportProgressDialog.cpp/.h`、`src/render/VideoExporter.*` | `src/next/app/ExportDialog.*`：摄像机+范围、1x/2x/4x 分辨率、1x/2x/4x fps、音频开关、输出路径、4x 大导出确认。进度是 `QProgressDialog` 文本帧数，没有预览/ETA。`ExportOptions` 固定 CRF 18，无编码器选择。 | 需要迁移 | 核心参数已覆盖且多了 2x/4x；旧“整画布”导出按新模型取消（必须选摄像机）。旧 CRF 档位、软件/VAAPI 编码器、导出画面预览和剩余时间仍是实际交付需要的控制；`export2::ExportOptions` 已有 `crf` 字段，接 UI 即可。 |
| 旧设置对话框（语言 / 主题 / 性能） | `src/app/SettingsDialog.cpp/.h` | `src/next/settings/SettingsDialog.*` 只有 HTML、视频、MCP 三页：缓存/清晰度/看门狗/Chrome、视频代理宽度/内存帧数、MCP 端口/token/端点/git/commit 延迟。 | 需要迁移 | HTML/视频/MCP 设置已覆盖新模型。旧的语言项见语言包一行；主题本来就没有选项（跟随系统 palette）；自动保存被自动 commit 取代，但吸附设置、工程默认值、时间轴缩放、解码信息、关于页仍值得补。 |
| 旧 MCP 日志 / 权限面板 | `src/ui/McpLogPanel.cpp/.h`、`src/app/SettingsDialog.cpp` MCP 页 | `src/next/mcp/McpLogPanel.*`：底部 dock、服务器/AI 状态、参数与结果详情、急停/恢复/清空、500 行/1500 字上限。权限模型改为 AI 默认可写、写时锁人类，不再按场景手动武装。 | 已覆盖 | 新 dock 满足 `ENTITY_MODEL.md` 的日志+急停要求。旧的场景级权限和写前 checkpoint 已被新权限模型取代，属于设计变更而非缺失；旧日志点击跳到实体/帧的小功能没搬，影响不大。 |
| 旧欢迎页 / 新建工程对话框 | `src/app/WelcomeWindow.cpp/.h`、`src/app/NewProjectDialog.cpp/.h` | `NextWindow::buildWelcome` 内嵌欢迎页：最近工程打开/移除/双击/右键。新建对话框含父目录、工程名、fps、宽高、背景色和预览。 | 已覆盖 | 新欢迎页比旧版多“移除”和背景色；旧作者/描述字段在新 `project.json` 中没有对应位置，可以放弃。 |
| 旧文字排版对话框 | `src/ui/TextLayoutDialog.cpp/.h`、`src/core/TextStyle.*`、`src/core/TextBlock.*` | Properties 文字页有文本、字体、字号、颜色、对齐、行高、字距、描边宽度；缺描边色、粗体/斜体/下划线、竖排、内边距/框宽/框高的静态编辑和实时预览对话框。 | 需要迁移 | 新模型文字字段齐全，`render2` 已支持，缺的是 UI。旧 `TextLayoutDialog` 的实时预览使用同一套 `TextStyle`，可按新模型改造后直接复用。 |
| 旧品牌墙生成器 | `src/ui/BrandWallDialog.cpp/.h`、`src/core/BrandWall.*` | 无对应 `ContentKind`。只能作为 `.pvhtml` 或矢量内容重建。 | 待定 | 保留就要新增内容类型 + JSON + `render2` 绘制 + 属性/工作台 UI；放弃则旧 PV 里的品牌墙镜头要用 HTML 重做。属于产品取舍，不是单纯搬代码。 |
| 旧漫画框 / 线条装饰 / 点阵 | `src/core/ComicBox.*`、`src/core/LineDeco.*`、`PropertiesPanel` 的漫画框/装饰线条/漫画点阵区、`Viewport` 尖角拖动 | 无 `ComicText / RadialLines / FlowLines / HalftoneDots` 内容类型。新模型只有 `empty / text / vector / camera / asset`。 | 待定 | 这是 PV 风格化的重要资产，整套迁移成本最高（模型 + 渲染 + UI）；也可以选择用 `.pvhtml` 重新实现。建议人类先定“哪些效果必须是一等公民”，再决定迁移范围。 |
| 旧环境面板 | `src/ui/EnvironmentPanel.cpp/.h` | 无环境面板。背景色只在新建工程对话框和 `project.json` 里，创建后只能用 MCP `project.update` 改。 | 需要迁移 | 改背景色是高频小操作，成本很低。建议并入项目属性或设置页，不必恢复独立面板。 |
| 旧波形视图 | `src/ui/WaveformView.cpp/.h`、`TimelinePanel::drawAudioClip` | 无波形视图，时间轴音频实体条也不画峰值。旧 `WaveformView` 在当前旧壳已被 `hide()`，且没有节拍标记。 | 需要迁移 | 独立波形页可以不搬，但旧 `drawAudioClip` 的峰值绘制应移植到新时间轴实体条；否则音频对齐只能靠听，剪辑效率明显下降。 |
| 旧视频代理 / 解码器设置 | `src/media/VideoProxy.*`、`src/media/DecoderInfo.*`、`SettingsDialog` 解码页 | Settings 视频页有代理开关、代理宽度、内存帧数；`src/video2` 负责代理与抽帧。解码信息页（只读诊断）没有搬。 | 已覆盖 | 旧壳是“后台自动代理、无 UI”，新壳升级成可配置。解码信息页只是诊断信息，缺了不影响编辑和导出。 |
| 旧快捷键 / 命令 | `MainWindow.cpp` 的 `createActions`、`TimelinePanel` / `CurveEditor` 的 `keyPressEvent` | 新壳有 Ctrl+Z/Y、Delete/Backspace、Space、Home/End、左右逐帧、Ctrl+,；时间轴 Delete/Esc 和关键帧 Ctrl+C/V 保留。缺 Ctrl+E 导出、F 适配、Ctrl+D 原地复制、Ctrl+C/V 实体复制粘贴、Ctrl+Shift+P 性能监视。 | 需要迁移 | 常用编辑命令的缺口会明显降低效率。保存快捷键可以不要（自动 commit），其余应在 `NextWindow` 补 QAction/Shortcut。 |
| 旧语言包（zh_CN / en_US） | `src/core/Lang.cpp/.h`、`assets/lang/zh_CN.json`、`assets/lang/en_US.json` | 新壳全部使用 `tr()`，但没有加载 `QTranslator`/`.ts`，实际只有中文硬编码；没有语言切换。 | 建议放弃 | 旧包只有 49 个 key，主要覆盖菜单，绝大多数对话框仍是硬编码中文，已经半途而废。新壳若要做英文，应重新用 Qt `.ts`，而不是迁旧 JSON。 |
| 旧项目检查点 / 历史 / 性能监视 | `src/core/CheckpointManager.*`、`src/core/History.*`、`src/core/PerfMonitor.*`、`MainWindow` 性能标签 | AI 每次写工具调用一个 git commit；人类内存 undo/redo；崩溃恢复。`src/next/history/HistoryPanel.h` 只写了面板、尚未接入；没有性能监视。 | 需要迁移 | checkpoint 已被 git commit 取代，不需要搬。git 历史面板和旧 `PerfMonitor` 状态栏仍缺，成本低，但对回滚和排障有用。 |
| 旧视口 Gizmo / 吸附 | `src/ui/Viewport.cpp/.h`、`SettingsDialog` 编辑器页 | `ViewportWidget`：局部 X/Y 箭头、四角旋转、Office 角缩放、中心缩放、pivot 点、负 scale、相机框。没有像素吸附、画面中心线吸附、其他实体边缘中点吸附和角度吸附。 | 需要迁移 | 新 gizmo 已按 `ENTITY_MODEL.md` 重写；旧 `snapPixels / edgeCenterSnap / edgeMidpointSnap / snapAngles` 是精确排版刚需，可在新 viewport 内复用旧判定逻辑。 |
| 旧工作台创建面板 | `src/ui/WorkbenchPanel.cpp/.h` | 实体菜单可建空/文字/矢量/摄像机，素材箱拖放建图片/视频/音频/HTML；没有浮动工作台和实时预览。 | 已覆盖 | 常用创建入口已由菜单+拖放覆盖。浮动工作台是旧交互皮肤；特殊内容另见漫画框/品牌墙两行。 |
| 旧 HTML 层 / 变量 | `PropertiesPanel` 的 HTML 区、`src/html/HtmlAsset.*`、`MainWindow` 的 HTML 渲染 | `src/html2` + `MediaServices` 负责 `.pvhtml` 三浏览器/缓存；AssetPanel 可导入 `.pvhtml`。Properties 没有 `var.*` 变量行/菱形，实体菜单也不能直接建 HTML 层（只能导入素材后拖放或用 MCP）。 | 需要迁移 | 新 HTML 管线明显更强，但变量是“关键帧驱动 HTML”的唯一入口。需在 Properties 增加 manifest 变量行，并补一个“新建 HTML 层”入口。 |
| 旧音频编辑（裁剪 / 淡入淡出） | `PropertiesPanel` 音频区、`src/audio/AudioEngine.cpp` | Properties 素材页有 `sourceIn`（帧）和 `volume`（可打关键帧）；`audio2` 混音只读 volume，不支持 `fadeIn/fadeOut`；时间轴裁剪会改 `sourceIn`。 | 需要迁移 | 旧壳的秒级 `trimStart/trimEnd`、淡入/淡出和“取播放头”对 PV 对齐很重要。新模型可先用 volume 关键帧模拟淡入淡出，但 UI 和混音需要补。 |
| 旧项目属性面板（名称 / 作者 / 描述 / 分辨率 / fps） | `src/ui/ProjectPropertiesPanel.cpp/.h` | 无项目属性 UI。新建时设置名称、宽高、fps、背景；之后只能用 MCP `project.update`。fps/分辨率按新模型创建后锁定。 | 需要迁移 | 名称和背景色应可改，作者/描述可丢。fps/分辨率锁定是设计选择，但至少要在 UI 里只读展示，否则用户不知道当前工程规格。 |

## 需要人类拍板的 Top 5

1. **特殊生成内容（漫画框 / 线条装饰 / 品牌墙 / 点阵）是否保留。** 迁移成本最高：要给新模型增加 4 个 `content.kind`、JSON 字段、`render2` 绘制、Properties 和创建入口；旧 `ComicBox/LineDeco/BrandWall` 的绘制代码可复用，但 UI 基本要重写。
2. **相机监视器（含临摹参考视频、HTTP/RTP 串流）是否保留。** 只做浮窗预览成本低；连 1479 行的 `CameraMonitor`、视频帧源和 ffmpeg 串流一起搬是中等工程，且会牵涉 `video2`/`MediaServices` 的帧供给。
3. **文字排版与缓动预设做到什么程度。** 30 种缓动是自包含移植（低成本、高收益）；文字排版要补描边色、粗体/斜体/下划线、竖排、内边距/框宽/框高和实时预览（中等成本），两者都直接影响成片观感。
4. **导出画质 / 编码器 / 进度预览是否补齐。** `export2` 已有 CRF 字段，ExportDialog 加档位和编码器下拉是低成本；导出预览和剩余时间要把帧回调接到进度对话框，属于中等成本，但影响最终交付体验。
5. **时间轴/音频编辑的 ergonomics 是否补齐。** 标记 CRUD 和音频峰值是低成本接线；淡入淡出、实体多选/框选要改 `model`/`audio2`/`timeline`，成本中等，但决定日常剪辑速度。
