> **AI 文档（非用户文档）**：本文由 AI/代理生成，面向 AI 与开发者，可能滞后于代码。用户请以 `README.md`、`docs/USAGE.md`、`docs/FEATURES.md` 为准。

# PVForge UI 回归对比：v0.2.0 → HEAD 58b16fa

> 只读对比。未修改主工作区代码，未 commit。对比时间：2026-11-11。
> 旧版工作区：`/tmp/pvforge-v020`（tag `v0.2.0` = `e994362`）
> 当前：`/home/xmb505/pvforge` HEAD `58b16fa`，二进制 `dist/pvforge`
> 截图根目录：`/tmp/opencode/ui-compare-v020/`
>
> **工作区状态（重要）**：对比时主工作区有 21 个未提交修改（`TimelinePanel.cpp` 的 `rowTop` bottom-up、`Gizmo.h` 的 Y-up 注释、`main.cpp` 的 P3b 自测等），文件修改时间 07:56–08:19，晚于 `dist/pvforge` 的构建时间 05:44。这些改动不在 HEAD `58b16fa`，也不在本次截图里；本报告只代表 HEAD / `dist/pvforge`。若它们正是并行进行的 #37/#39 修复，落地后 §3.2 的 row 0 与 §3.4 的 Gizmo 两行必须用新构建复测。全程未修改、未提交这些文件。

## 1. 对比方法

### 1.1 构建与运行

```bash
# 旧版
git worktree add /tmp/pvforge-v020 v0.2.0
cd /tmp/pvforge-v020
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=OFF
cmake --build build --target pvforge -j

# 截图统一 offscreen，避免窗口管理器干扰
QT_QPA_PLATFORM=offscreen <binary> --<test>
```

- 旧版二进制：`/tmp/pvforge-v020/build/pvforge`
- 当前二进制：`/home/xmb505/pvforge/dist/pvforge`
- 同一套 25 个共用自测开关在旧/新各跑一遍：`gizmo-test`、`pivot-test`、`pivot-gizmo-test`、`camera-test`、`camera-parent-test`、`monitor-feature-test`、`easing-test`、`settings-test`、`timeline-interaction-test`、`timeline-rowbuttons-test`、`timeline-zoom-test`、`timeline-frame-test`、`open-timeline-test`、`library-test`、`workbench-test`、`special-test`、`shape-test`、`guide-test`、`folder-test`、`self-test`、`scale-handle-test`、`snap-test`、`entity-snap-test`、`layout-test`、`welcome-test`。
- 旧版 25/25 通过；当前 19/25 通过，另 `keyframe-drag-test`、`curve-editor-test` 也未通过。失败项判读见 §6，不能全部当作 UI 回归（部分测试仍用旧固定像素/旧行数假设）。

### 1.2 分辨率与工程条件

| 截图组 | 分辨率 | 工程/入口 | 说明 |
|---|---|---|---|
| `old/demo-f0.png`、`new/demo-f0.png` | 1440×900 | `--demo-shot`，内存 `Project` | 同一段图片动画，最适合比布局 |
| `old/layout-narrow.png`、`new/layout-narrow.png` | 1000×1060 | `--layout-test` | 输出停靠区/视口几何 |
| `*-suite/pvforge_workbench*.png` | 1440×900 | `--workbench-test` | 工作台创建面板 |
| `*-suite/pvforge_gizmo/handles/camera*.png` | 1200×800 | 旧式 `MainWindow(Project*)` | 旧 gizmo/相机路径，仅作视觉基准 |
| `new/02-timeline-session.png` | 1200×800 | 新版 `--timeline-test`（目录工程 + `EditorSession`） | 实际交付路径的新时间轴 |
| `new/04-gizmo-session.png` | 1200×800 | 新版 P3b session gizmo 测试 | 实际交付路径的新 gizmo |
| `new/11-viewport-render.png` | 1200×800 | 新版 `--viewport-test`（目录工程） | 新会话视口/原点 |
| `new/05-properties.png` | 1200×800 | 新版 `--properties-test`（目录工程） | 新属性面板 |
| `new/07-assets.png` | 1440×900 | 新版 `--assets-test`（目录工程） | 新素材面板 |
| `new/08-mcp-log.png` | 1440×900 | 新版 `--mcp-ui-test` | MCP 日志浮层 |
| `new/10-export-dialog.png` | 566×420 | 新版 `--export-dialog-test` | 旧版无导出 UI |

**无法同条件对比的项**

1. **工程文件格式不同**：v0.2.0 只支持单文件 `.pvproj`；当前是目录工程（`project.json` + 素材 + git）。无法用同一个工程文件直接开两次。优先用各自自测模式，截图中的工程内容因此不完全相同。
2. **旧版没有的界面**：导出对话框、MCP 日志、MCP 设置页、求解段、曲线编辑器、Git 历史。当前截图只能说明“新增”，不能判回归。
3. **测试入口不同**：当前大量 UI 测试仍用旧式 `MainWindow(Project*)`（legacy 路径），而真实产品打开目录工程走 `MainWindow(EditorSession*)`（session 路径）。原点、gizmo、时间轴关键帧、素材箱等回归只在 session 截图（`new/02`、`new/04`、`new/11`）里出现；legacy 截图反而保留旧行为。报告里已逐项注明。
4. **快捷键未逐项实测**：只验证了测试用到的 Delete/Ctrl+C/V/方向键，以及菜单/工具栏可见项；没有穷举全部快捷键冲突。
5. **悬停/右键菜单只抽查**：关键帧、片段、标记菜单已看；其他右键项未逐项对比。

### 1.3 截图目录

- `old-suite/`：旧版 25 个共用测试产出的全部截图（53 张）
- `new-suite/`：当前 25 个共用测试产出的全部截图（61 张）
- `old/`、`new/`：欢迎页、新建工程、布局窄窗、demo，以及当前 session 路径的关键截图
- `cmp-*.png`：旧左 / 新右的并排对比图（30 张）
- `logs/`：两套测试完整日志

## 2. 结论摘要

共记录 **27 项差异**：判为回归 **17 项**（其中 P0 5 项），其余为设计变更、新功能或无法同条件对比。表中“row 0 在上”和“曲线/求解/四角柄”等标了严重度，但属于“需求未实现/新功能未达标”，不计入 17 项回归。

**最严重的 5 项**

1. **坐标系原点从画布中心变成左上角**（#44）：目录工程里新元素/MCP 写入的 `(0,0)` 落在画布左上角，常有一半在画布外；旧版 `(0,0)` 在画布中心。
2. **Gizmo Y 轴朝下**（#37）：旧版 X 向右、Y 向上；当前 session gizmo X 向右、Y 向下，旋转后整个轴向基座相对旧版上下镜像。
3. **工作台创建按钮被裁掉**（#6）：底部停靠区从 220px 涨到 380px，1440×900 下视口只剩约 394px 高，640×560 的工作台面板底部被裁，`创建` 按钮不可见。
4. **默认“片段”模式下关键帧不可选、不可拖**：旧版点关键帧菱形即选中/定位播放头；当前必须先点“片段”切到“关键帧”模式，否则点菱形会当成拖 clip。
5. **素材库从左/右属性面板移到底部，且“创建实体/删除”按钮被隐藏**：旧版素材库是右侧“项目属性”里的折叠段；当前变成时间轴左侧的“全部素材”，可见工作流改成拖到时间轴。

## 3. 差异表

严重度：P0 阻断常见工作流；P1 明显可用性/布局回归；P2 视觉或手感问题。

### 3.1 主窗口与布局

| 屏幕/交互 | v0.2.0 | 当前 | 差异/影响 | 严重度 | issue |
|---|---|---|---|---|---|
| 主窗口纵向分栏 | 时间轴 220px 高，全宽；视口 676×750（1000×1060） | 时间轴 380px 高，且只占 x=304..1000；视口 676×590 | 底部停靠区多 160px；1440×900 下视口约 555→394px。画布变小，工作台被裁 | P0 | 新 issue（关联 #6） |
| 素材库位置 | 右侧“项目属性”页签里的“素材库”折叠段（320px 宽），可见“创建实体/删除” | 底部时间轴左侧“全部素材”304px；`m_create`/`m_remove` 被 `hide()`，只能右键删除；创建改为拖到时间轴 | 旧面板/入口消失，违背“保留旧布局/面板” | P1 | #5、新 issue |
| 时间轴左列 | 130px 名称列：`搜索对象…` + 每行实体名 + 眼睛/锁按钮 | 无名称列、无实体搜索；名称画在 clip 内部 | 短 clip 名称被省略（相机 clip 只剩 “M▸”）；按名字找实体困难 | P1 | #25、新 issue |
| 顶部工具栏 | 播放、自动关键帧 | 播放、回到开头、跳到结尾、适配窗口、自动关键帧 | 新增快捷按钮，不判回归 | — | 新功能 |
| 状态栏 | `帧 0 / 30 · 30 fps` | `0.00s / 1.00s · 30 fps`，另有 “MCP 关闭 · 日志” | 帧级定位读数丢失；MCP 状态为新增 | P2 | 新 issue |

### 3.2 时间轴

| 屏幕/交互 | v0.2.0 | 当前 | 差异/影响 | 严重度 | issue |
|---|---|---|---|---|---|
| 标尺刻度 | 1/5/10/50/200 帧密度表；主刻度之间有小刻度 | 只画主网格线；标签步长按 `while (step*px<64) step*=2` 计算，1200px 下常见 16 帧一标（截图 `0,16,32…`） | 小刻度消失、帧密度变了；`--timeline-frame-test` 20 处失败 | P1 | #11、新 issue |
| 行方向 / row 0 | row 0 在顶部（上→下） | 仍在顶部；`rowTop(row)=ruler+row*kRowHeight`，row 越大越靠下 | **与 v0.2.0 相同，不是本次回归**；但明确违反“row 0 在最底、从下往上”的 #39 | P1（需求未实现） | #39 |
| 关键帧编辑 | 任意时刻点菱形即选中、定位播放头；右键/属性“缓动”编辑 | 默认 `EditMode::Clips` 下菱形仅显示；选中/拖动/框选只在 `EditMode::Keyframes` 生效 | 多一步切模式；默认点菱形会开始拖 clip。`--keyframe-drag-test`/`--timeline-interaction-test` 失败 | P0 | 新 issue（关联 #42） |
| 行隐藏/锁定 | 名称列右侧眼睛/锁按钮，点击切换 | 无按钮；只在 clip 上画“锁/隐”文字徽标 | 入口消失。`--timeline-rowbuttons-test` 的 hide/lock/boxed 全部失败 | P1 | #25 |
| 单位/模式按钮 | 左上“帧 / 秒”切换 | 右上“片段 / 秒”两个按钮；“片段”实际是 clip/keyframe 模式切换 | 文案和位置都变，肌肉记忆破坏 | P2 | 新 issue |
| 片段外观 | 行/条矩形直角，实体名在左列 | `drawRoundedRect(..., 5, 5)` 圆角 + 渐变 + 白色高光 | 与“像素风、直角”偏好冲突；短 clip 文字被 “+” 挤掉 | P2 | 新 issue |
| 私有时间线/子级 | 文件夹在行内展开为树，子行 inline | 文件夹只显示一条 clip；子级在浮层“私有时间线”里 | 符合“私有时间线用浮层”的新需求，但主时间轴不再有旧 inline 树；需确认是否接受 | — | 设计变更（#2） |
| 片段拖动/裁剪 | 实体行拖动/裁剪；`timeline-interaction` 通过 | clip 拖动/两端裁剪；`--timeline-edit-test` 通过 | 核心拖动/裁剪可用；默认关键帧交互另见表 | — | — |
| 关键帧缓动入口 | 关键帧右键菜单 + 属性面板“缓动” | 同样在关键帧右键菜单 + 属性面板“缓动”；未发现 clip 级入口 | **#42 未复现**；但默认 clip 模式下关键帧点不中，用户会以为入口在 clip 上 | — | #42（建议复核关闭） |
| 曲线编辑器 | v0.2.0 没有 | `CurveEditor` 源码在，但 `--curve-editor-test` 报 “timeline tabs/curve editor missing” | 新增功能当前不可达；不是 v0.2.0 回归，但 #40 无法验收 | P1（新功能） | #40 |

### 3.3 属性面板

| 屏幕/交互 | v0.2.0 | 当前 | 差异/影响 | 严重度 | issue |
|---|---|---|---|---|---|
| 文字/矢量内容区 | 800px 高窗口内，“文字（内容/字体/字号）”或“矢量（形状/圆角/边数）”可见 | 被“变换 + 关键帧（含自动关键帧/逐行菱形）”挤出首屏，要滚动 | 常用内容编辑多一步滚动；旧版是“打开就能改” | P1 | #13 |
| 逐行关键帧菱形 | 无；只有“在当前帧打关键帧” | 每行右侧 `◇` 按钮；相机“取景比例”下方额外出现两个无标签菱形 | 新功能本身合理，但相机页两个悬空菱形是布局 bug | —（新功能；悬空按钮另计 P2） | 新 issue |
| 相机属性 | 变换 → 关键帧；无游离控件 | 变换 → 取景比例 → 两个空菱形 → 关键帧 | 视觉脏，疑似给不存在行创建的 key 按钮 | P2 | 新 issue |
| 求解段 | 无 | 新增“求解段”折叠段；“结束帧”默认 `-1`，特殊值显示“片段末帧” | #43 的“强制选结束帧”已基本解决；没有轨迹预览/初速度手柄 | P1（新功能） | #43（部分）、#38 |
| 项目设置 | 名称/作者/描述/宽高/帧率；另有“素材库” | 名称/宽高/帧率；作者/描述删除；无“素材库” | 作者/描述是后端模型变化，按 `UI_BACKEND_INTEGRATION.md` 属有意；素材库移走见 §3.1 | — | 设计变更 |

### 3.4 视口与 Gizmo

| 屏幕/交互 | v0.2.0 | 当前 | 差异/影响 | 严重度 | issue |
|---|---|---|---|---|---|
| 坐标原点 | `(0,0)` = 画布中心；`entityScreenCenter = canvas.center() + (x,y)*s` | session 路径 `sessionWorldToScreen = translate(canvas.left(), canvas.top())`；`(0,0)` = 画布左上角 | 新元素/MCP 数据落在左上，常一半在画布外；`new/02`、`new/11` 可见 HTML/图片贴左上 | P0 | #44 |
| Gizmo 轴向 | X 向右、Y 向上；不随实体旋转 | session gizmo 的 `MoveY = pivot + rotate((0,+len))`，屏幕 Y 向下；X 随 rotation 顺时针 | 绿色 Y 箭头朝下；旋转后整个轴向与旧版上下镜像。`new/04-gizmo-session.png` 可直接看到 | P0 | #37 |
| 选择框 | 实线蓝框 + 四角白色方块 + 单个浅黄旋转环 | 青色虚线框 + 四角内缩白方块 + 中心缩放方块 + 四角圆弧旋转柄；无旋转环 | 视觉风格变化；旧“拖环旋转”不再存在，改用四角圆弧 | P1 | #41 |
| 四角旋转手柄 | 无四角旋转符号；只有一个旋转环 | `drawRotateGlyph` 画两段圆弧（无箭头），放在四角外侧 | 不是 Inkscape 的弯双箭头；命中/尺寸/光标未验证 | P1（新功能） | #41 |
| 相机框/监视器 | 虚线相机框 + 4 角手柄 + 顶部旋转圆点；监视器右下浮动 | 相同 | 相机框、监视器位置基本一致，未发现回归 | — | — |
| 画布外扩展区 | 无；画布外是窗口底色 | 默认 `workbenchCacheExpandPercent=50`，扩展区透明（截图显示为白/浅色边） | 需求要“画布外实体可见”；但白边会让人以为画布缩水，建议扩展区用暗色 | P2 | 新功能 |
| 坐标轴/参考线 | 画布中心两条白色虚线 | 相同（中心参考线仍在） | 参考线没变；但 gizmo/渲染原点已变，参考线不再代表坐标原点 | —（随 #44） | #44 |

### 3.5 工作台 / 素材 / 对话框

| 屏幕/交互 | v0.2.0 | 当前 | 差异/影响 | 严重度 | issue |
|---|---|---|---|---|---|
| 工作台创建面板 | 固定 640×560，1440×900 下完整显示，底部“创建”可点 | 同样 640×560；1440×900 视口只有 ~394px 高，面板底部被裁，“创建”不可见 | 测试仍能 `click()` 被裁按钮，所以自测通过，但用户看不到 | P0 | #6 |
| 工作台页面 | 媒体/文字/文本框/矢量/特殊/摄像机 | 相同，另加扩展页 | 基本保留；无内容级回归 | — | — |
| 素材面板 | 预览 + 搜索 + 列表 + 详情 + 创建实体/删除 | 全部素材 + 网格/搜索/+ /H + 标签筛选 + 列表 + 详情；创建/删除按钮被隐藏 | 旧入口消失；创建改为拖到时间轴。测试仍驱动隐藏按钮，`--library-test` 假通过 | P1 | #5、新 issue |
| 欢迎页 | PVForge 0.2.0 + 最近项目 | 相同布局；版本仍 0.2.0；无最近项目占位 | 无回归 | — | — |
| 新建工程 | 名称/位置/分辨率/帧率/作者/描述 | 名称/位置/分辨率/帧率；作者/描述删除；增加路径预览和“创建后锁定”说明 | 按 `UI_BACKEND_INTEGRATION.md` 属有意；旧用户会少两个字段 | — | 设计变更 |
| 设置 | 常规/编辑器/新项目/时间轴/素材/解码/关于 | 增加“MCP”页，其余页签/尺寸一致 | 新功能，无回归 | — | 新功能 |
| 导出 | v0.2.0 无导出 UI | 摄像机/范围/输出文件/分辨率/fps/CRF/编码器/preset/音频 | 无法同条件对比；新版为新增 | — | #10 |
| MCP 日志 | 无 | 视口左下浮层，默认盖住视口下缘和时间轴上缘 | 新功能；与相机监视器同为视口内浮动层，容易互相遮挡，建议改成可停靠面板 | P2（新功能） | 新功能 |
| 求解段轨迹预览 | 无 | 视口/属性均无轨迹与初速度手柄 | #38 未实现 | P1（新功能） | #38 |

## 4. 重点确认（逐项）

| 确认项 | v0.2.0 | 当前 | 判定 |
|---|---|---|---|
| 坐标原点/方向 | `(0,0)` 在画布中心；+X 右、+Y 屏幕下（旧视口 `canvas.center()+(x,y)*s`） | session `(0,0)` 在画布左上；渲染/覆盖层/MCP 同一套左上原点 | **回归**（#44）。旧 UI 的“新建元素出现在中心”不再成立；`new/11` 里 HTML/图片/文字都贴左上，部分出画布 |
| Gizmo 轴向 | X 向右、Y 向上；不随旋转 | X 随 rotation 顺时针，Y 朝屏幕下；旋转 30° 时 X 右下、Y 左下 | **回归**（#37）。不是“不跟随旋转”，而是基座相对旧版上下镜像；拖动方向/符号需按旧坐标语义重定 |
| 时间轴 row 0 方向/拖动 | row 0 在顶部；行内树；拖动实体行 | row 0 仍在顶部（`rowTop` 递增）；clip 可自由拖到行/时间；`--timeline-edit-test` 通过 | **不是 v0.2.0 回归**，但 **#39 未实现**。当前只满足“自由拖动”，没满足“从下往上、row 0 最底” |
| 关键帧曲线/缓动 UI | 关键帧右键 → 缓动选择器；属性面板“缓动”；无曲线页 | 关键帧右键 → 缓动；属性“缓动”；默认 clip 模式点不中关键帧；`CurveEditor` 不可达 | **部分回归**：缓动仍归关键帧（#42 未复现），但默认模式让入口“看起来在 clip 上”；曲线整体视图（#40）不可用 |
| 求解段结束帧/预览 | 无求解段 | 结束帧可设 `-1`=片段末帧；无轨迹预览/初速度手柄 | **不是回归**（旧版没有）；#43 基本解决，#38 仍缺 |
| 四角旋转手柄 | 单个旋转环，四角只有缩放手柄 | 四角外侧两段圆弧；无箭头、无旋转环 | **不是回归**（旧版没有四角柄），但 #41 未达标；旧的“拖环”手感丢失 |

## 5. “不是回归”的差异（新后端/新需求有意变化）

1. **clip 时间轴/PR 式片段**：旧版是实体行列表，当前是 clip 条 + 私有时间线浮层；来自 #2/#35 的时间轴需求。注意它连带造成“名称列/实体搜索/隐藏锁定按钮”消失，这些应作为回归单独修。
2. **时间轴 row 0 在上**：v0.2.0 也是上→下；不是本次接后端造成的，但 #39 要求改成从下往上。
3. **新建工程去掉作者/描述**：新模型没有这两个字段，`UI_BACKEND_INTEGRATION.md` 明确要删。
4. **设置新增 MCP 页、导出对话框、MCP 日志、Git 历史、求解段、曲线编辑器**：v0.2.0 没有对应界面，无法判回归。
5. **工具栏新增“回到开头/跳到结尾/适配窗口”**：新增快捷操作，不破坏旧按钮。
6. **画布外 50% 扩展缓存**：新需求“画布外实体可见”，不是回归；白边建议改暗色。
7. **素材创建改为拖到时间轴**：新工作流，但旧“创建实体/删除”按钮被隐藏属于回归（见 §3）。
8. **相机监视器位置/行为**：与 v0.2.0 基本一致，未发现回归。

## 6. 自测结果与判读

### 6.1 结果

- 旧版 25 个共用测试：**25/25 通过**。
- 当前 25 个共用测试：**19/25 通过**；失败 6 个：`camera-parent-test`、`timeline-interaction-test`、`timeline-rowbuttons-test`、`timeline-frame-test`、`folder-test`、`snap-test`。
- 当前额外失败：`keyframe-drag-test`、`curve-editor-test`。

### 6.2 判读（不要直接把失败数当回归数）

| 测试 | 失败内容 | 判读 |
|---|---|---|
| `timeline-rowbuttons-test` | boxed 0 / select all 0 / hide failed / lock failed | **真实回归**：行按钮被删，只剩徽标（代码中无 eye/lock 绘制） |
| `timeline-interaction-test` | ruler 83（期望 53）、empty 50（期望 20）、keyframe 未聚焦/未选中 | **混合**：固定 x 坐标因时间轴左移 304px 失效；但默认 clip 模式不选关键帧是真实回归 |
| `timeline-frame-test` | 20 处标尺步长/秒格失败 | **真实回归**：新标尺不再用旧 1/5/10/50 密度表，且无小刻度 |
| `keyframe-drag-test` | selection 0 / playhead 不跟随 | **真实回归**：默认 clip 模式点菱形不选关键帧（需切“关键帧”模式） |
| `camera-parent-test` | tree rows 15（期望 4） | **测试假设问题**：`rowIds()` 会给空行补 padding，行数不再等于实体数；相机跟随/屏幕位置检查未报错 |
| `folder-test` | tree rows 15 / collapse 15 / drag into folder -1 | **混合**：行数 padding 是测试假设；拖拽起点在旧行按钮位置（已删除），所以行头拖拽失效；clip 拖拽/改父级由 `timeline-edit-test` 覆盖并通过 |
| `snap-test` | scale snap 6.331 vs 6.400（差 0.069） | **疑似像素取整/视口尺寸变化**，不是明确 UI 回归；建议单独复测 |
| `curve-editor-test` | timeline tabs/curve editor missing | **新功能不可达**：曲线页签被删，源码还在 |

## 7. Top 10 必须修

1. **#44 原点回画布中心**（P0）：`sessionWorldToScreen`/render2/MCP/导出统一为中心原点；新元素默认 `(0,0)` 落在中心。验收：新建 HTML/图片/文字出现在画布中心；`p2a` 的 HTML 不再贴左上。
2. **#37 Gizmo Y 轴翻回向上**（P0）：`gizmo::handlePosition` 的 MoveY 用 `(0,-len)`；旋转后 X/Y 与旧视口同号。验收：rotation=0 绿箭头向上；rotation=30° 时轴随实体转，符号与旧版一致。
3. **#6 工作台创建按钮可见**（P0）：恢复底部停靠区 220px 左右，或让工作台面板可滚动/自适应；1440×900 下“创建”必须可点。
4. **恢复关键帧直接编辑**（P0）：默认模式点菱形即选中/定位播放头；或把关键帧模式设为默认并给出明确切换提示。验收：`--keyframe-drag-test`、`--timeline-interaction-test` 在默认模式通过。
5. **素材库回右侧“项目属性”**（P1）：恢复“素材库”折叠段和可见的“创建实体/删除”按钮；底部只保留时间轴。若保留拖拽工作流，两个入口并存。
6. **底部停靠区/视口高度恢复**（P1）：按旧版 `mainSplit {640,220}`，视口 676×750（1000×1060）；时间轴不再挤占 160px。
7. **恢复行隐藏/锁定按钮**（P1，#25）：名称列或 clip 左侧提供眼睛/锁；保留现有“锁/隐”徽标作为状态提示。
8. **恢复标尺密度表与小刻度**（P1，#11）：1/5/10/50/200 帧步进 + 主刻度间小刻度；标签居中（#11 原要求）。
9. **恢复名称列/实体搜索 + 属性内容首屏**（P1）：时间轴左列 130px 名称列和 `搜索对象…`；属性面板把文字/矢量内容放回首屏（压缩逐行菱形/关键帧段），删除相机页悬空菱形。
10. **#39 时间轴从下往上**（P1）：row 0 放最底，`rowTop` 改为从底部递增；保留 free drag。验收：两行实体时新实体在上、row 0 在下。

## 8. 截图索引（关键项）

| 内容 | 旧版 | 当前 | 并排 |
|---|---|---|---|
| 主窗口 + 时间轴布局 | `old/demo-f0.png` | `new/demo-f0.png` | `cmp-demo-f0.png` |
| 窄窗布局/视口 | `old/layout-narrow.png` | `new/layout-narrow.png` | `cmp-layout-narrow.png` |
| 工作台/创建面板 | `old-suite/pvforge_workbench.png` | `new-suite/pvforge_workbench.png` | `cmp-pvforge_workbench.png` |
| 时间轴帧模式 | `old-suite/pvforge_timeline_frames.png` | `new-suite/pvforge_timeline_frames.png` | `cmp-pvforge_timeline_frames.png` |
| 时间轴秒模式 | `old-suite/pvforge_timeline_seconds.png` | `new-suite/pvforge_timeline_seconds.png` | `cmp-pvforge_timeline_seconds.png` |
| 行按钮/隐藏锁定 | `old-suite/pvforge_row_buttons.png` | `new-suite/pvforge_row_buttons.png` | `cmp-pvforge_row_buttons.png` |
| 素材库/素材面板 | `old-suite/pvforge_library.png` | `new/07-assets.png` | `cmp-pvforge_library.png` |
| 属性面板（文字/矢量） | `old-suite/pvforge_types.png` / `pvforge_vector.png` | `new-suite/pvforge_types.png` / `pvforge_vector.png` | `cmp-pvforge_types.png` / `cmp-pvforge_vector.png` |
| 相机属性（悬空菱形） | `old-suite/pvforge_camera.png` | `new-suite/pvforge_camera.png` | `cmp-pvforge_camera.png` |
| 相机旋转/相机框 | `old-suite/pvforge_camera_rotated.png` | `new-suite/pvforge_camera_rotated.png` | `cmp-pvforge_camera_rotated.png` |
| 会话 gizmo（原点/Y 轴） | 旧版无 session；用 `old-suite/pvforge_gizmo.png` 作轴向上/右基准 | `new/04-gizmo-session.png` | 见 §4 说明 |
| 会话视口（元素贴左上） | 旧版无 session；用 `old-suite/pvforge_layout_narrow.png` 作中心原点基准 | `new/11-viewport-render.png` | 见 §4 说明 |
| 时间轴 session | 旧版 `old-suite/pvforge_open_timeline.png` | `new/02-timeline-session.png` | — |
| 设置 | `old-suite/pvf_settings.png` | `new-suite/pvf_settings.png` | `cmp-pvf_settings.png` |
| 导出 | v0.2.0 无 | `new/10-export-dialog.png` | — |
| MCP 日志 | v0.2.0 无 | `new/08-mcp-log.png` | — |

> 注：`new-suite/pvforge_gizmo.png`、`new-suite/pvforge_camera.png` 等走的是 legacy `MainWindow(Project*)` 路径，因此它们的 gizmo/原点仍是旧行为，不能用来证明 session 路径正常；session 证据以 `new/02`、`new/04`、`new/11` 为准。
