> **AI 文档（非用户文档）**：本文由 AI/代理生成，面向 AI 与开发者，可能滞后于代码。用户请以 `README.md`、`docs/USAGE.md`、`docs/FEATURES.md` 为准。

# M6：HTML 组件与 MCP

记录时间：2026-11-08
状态：开工；M5-1/M5-2 已完成，M6 按本文档推进
前置阅读：[MILESTONE.md](MILESTONE.md)、[FEATURES.md](FEATURES.md)、[USAGE.md](USAGE.md)
配套：[AI_AUTHORING.md](AI_AUTHORING.md)（AI 的 2D 工作流指南）、[M6_HTML_API.md](M6_HTML_API.md)（HTML 组件契约）、[examples/html](../examples/html/)（可直接导入的示例资产）

## 一句话

让 PVForge 能渲染 AI 写的前端组件（HTML/CSS/JS），并让 AI 通过 MCP 安全地操作工程：默认只读、显式解锁、场景边界、急停回滚、批次检查点，人类始终看得见 AI 在做什么。

## 已定决策

| 领域 | 决策 |
|---|---|
| 浏览器 | 外部 Chromium/CDP 主后端；自动探测 Chrome/Edge/Chromium + 手动指定二进制；找不到时禁用 HTML 层并提示 |
| QtWebEngine | 只保留抽象接口，不在 v1 实现；后续有需求再评估 |
| HTML 交付 | 必须进视口、监视器和最终 MP4 |
| 合成 | 共享 `HtmlLayer`：一层 = 一个页面 = 一个 HTML 资产 = 一次抓帧；后续可多层 |
| 代码契约 | 一层一个完整 HTML 文档 + manifest；宿主不拼 snippet |
| 变量 | `组件名.变量名` 命名空间；double + schema；复用 Track/Easing |
| 确定性 | 纯函数；禁用 CSS 动画/自由时间；同一帧两次逐像素一致 |
| 预览 | headless 浏览器 + CDP 截图 + 位图合成；按显示尺寸抓，播放时 15fps 上限 |
| 导出 | 异步帧供应器，逐帧等待抓图，不节流 |
| MCP 客户端 | 优先 OpenCode / DeepSeek harness；Streamable HTTP 为主，stdio 桥按需 |
| MCP 权限 | 默认只读；显式“允许修改”；AI 只写当前场景 + 全局对象；禁止删除/文件/命令/导出/改设置 |
| 急停 | 中止当前调用、清空队列、恢复批次快照，AI 收到 `stopped_by_human` |
| 人类操作 | AI 操作期间可拖播放头/播放/缩放平移/切摄像机，不能编辑 |
| 回滚 | 每次调用原子 + 每批隐藏 git checkpoint + 恢复列表；不碰用户仓库 |
| 注释 | 全局标记轨 + 关键帧备注；AI 只读 |
| 场景 | 一等公民：名称 + 帧区间 + 根文件夹；不重叠、可留空；AI 默认只写当前场景 |
| 工程格式 | 现状 `version: 3`，新版升到 **4**；加载器兼容旧版，新版拒绝旧应用 |
| 组件编辑 | 人类不改代码；HTML 是只读素材，可整份替换；同名变量保留关键帧 |

## 总体架构

```
AI 客户端 (OpenCode / DeepSeek harness)
        │  Streamable HTTP / stdio 桥
        ▼
   McpServer ── 权限/场景边界/限额 ── ToolRegistry
        │
        ▼  UI 线程事务
 Project v4 ── History + 批次快照 ── 隐藏 git checkpoint
        │
        ├─ HtmlLayer 实体 ── 变量求值 ── HtmlRenderer
        │                                   │ CDP
        │                           Chrome/Edge headless
        │                                   │ 截图
        │                                   ▼
        │                              QImage 缓存
        │                                   │
        ▼                                   ▼
   SceneRenderer ──────────────────── 视口 / 监视器
        │
        └─ VideoExporter（异步帧供应器）── ffmpeg ── MP4
```

## 1. HTML 组件

### 1.1 浏览器选择与生命周期

设置 → HTML：

- 自动探测 Chrome/Edge/Chromium 常见路径；
- 手动路径框 + 浏览 + “测试”按钮（启动 headless，返回版本）；
- 状态：可用/缺失/版本/最后错误；
- 预览帧率：自动 / 15 / 30；
- 清除缓存。

规则：

- 浏览器不随应用分发、不自动下载；只调用用户已安装的 Chrome/Edge。
- 首次需要渲染 HTML 层时启动：
  - `--headless=new`、独立用户数据目录、禁用扩展/同步/媒体/网络；
  - 一个浏览器进程，每层一个 page；
  - 应用退出、浏览器崩溃、路径变更时清理/重启。
- 找不到浏览器：已有 HTML 层显示占位框，新建 HTML 层禁用，设置页给安装提示；普通工程不受影响。

### 1.2 实体与资产

新增 `EntityType::HtmlLayer`：

- 普通时间轴行，有位置/旋转/缩放/透明度/父级/锁定/隐藏；
- `data`：资产 id、逻辑尺寸、是否时间相关；
- 变量轨：`var.<组件>.<变量>`。

HTML 资产是单个内容寻址 JSON：`assets/html/<sha>.json`：

```json
{
  "format": "pvforge-html",
  "version": 1,
  "html": "<!doctype html>...",
  "manifest": {
    "size": [800, 120],
    "transparent": true,
    "timeDependent": false,
    "components": [
      {
        "id": "progress",
        "name": "进度条",
        "variables": [
          {"name": "value", "type": "number", "min": 0, "max": 100,
           "default": 0, "integer": true}
        ]
      }
    ]
  }
}
```

- 字体/图片/SVG 以 data URL 内联；或引用工程内资产，由宿主内联后再加载。
- 素材库：HTML 资产带缩略图、名称、尺寸、变量列表；可预览、复用、整份替换。
- 被实体引用的资产不能直接删除；先换掉或移除引用。
- 换素材：按变量名匹配，保留同名轨；新变量用默认值；旧变量轨保留但标记“未使用”；一条撤销。

### 1.3 变量与关键帧

- manifest 声明变量：`name`、`type: number`、`min`、`max`、`default`、`integer`（可选）。
- 内部一律 double；整数只是 UI 显示。
- 属性面板“HTML”区按组件分组；每行显示当前值、范围、◇/◆；改动走现有自动关键帧逻辑；接入 M5 曲线编辑器。
- 变量插值复用 `Track` + `EasingSpec`；二次缓动已有 `EaseIn/Out/InOutQuad`。
- AI 通过 MCP 创建变量、设置当前值、批量设置关键帧和缓动。

### 1.4 组件 JS 契约

宿主只暴露：

- `pvf.vars`：当前帧变量值，只读；
- `pvf.frame`、`pvf.time`：当前帧与秒数；
- `pvf.width`、`pvf.height`：逻辑尺寸；
- `pvf.asset(name)`：工程内资产 URL；
- `pvf.ready()`：初始化完成；
- `pvf.rendered()`：本帧渲染完成。

组件约定：

- 画面只由 `pvf.vars` / `pvf.frame` / `pvf.time` 决定；
- 每帧宿主更新 `pvf.vars` 并调用 `pvf.render(vars)`（可选）；组件完成后调用 `pvf.rendered()`；
- 不允许网络、文件、媒体播放、自由 `requestAnimationFrame` 动画、CSS animation/transition；
- v1 允许 DOM/CSS/SVG/Canvas 2D；禁用 WebGL、视频、音频；
- 超时（默认 1 秒）视为渲染失败：预览显示占位并记日志，导出直接报错，不输出错帧。

### 1.5 确定性

宿主每帧注入：

- 固定 `Date.now` / `performance.now`；
- 按帧 seed 的 `Math.random`；
- 禁用 CSS 动画/过渡的样式；
- 等 `document.fonts.ready`；
- 等 `pvf.rendered()` + 两次 `requestAnimationFrame`；
- 再 `Page.captureScreenshot`。

验收：同一帧连续渲染两次，PNG 像素哈希一致；导出同一帧两次，解码后像素一致。

### 1.6 预览与导出

视口/监视器：

- 按当前帧求值变量；
- CDP `Runtime.evaluate` 注入；
- 等 `rendered`；
- `Page.captureScreenshot` 抓这一层（PNG/base64）；
- 解码 QImage，按实体变换合成；
- 只保留一张在途请求，新帧覆盖旧帧；
- 播放时按显示尺寸抓，15fps 上限；暂停时全质量；隐藏/透明 0 跳过。

导出：

- `VideoExporter` 改为异步帧供应器：请求一帧 → 等 HTML 抓帧 → 合成 → 写 ffmpeg → 下一帧；
- 不节流，逐帧等待；超时/失败立即停止并清理 `.part.mp4`；
- 进度、fps、剩余时间沿用现有预览窗。

分辨率：

- 预览按屏幕显示尺寸抓，设上限；
- 导出按输出分辨率 × 工程比例抓，上限 4096；
- 缓存 key：资产 hash + 抓帧尺寸 + 变量值 + frame/time（仅时间相关组件）。

### 1.7 安全与限制

- 断网：Chrome 启动参数 + CDP 拦截所有非 data:/内联请求；所有资源内联。
- 禁止 `file://`、本地路径、外部进程、下载、媒体、WebGL。
- v1 限额：单层 HTML ≤ 2MB；单资产 ≤ 5MB；单层组件 ≤ 16；同时 ≤ 2 个 HTML 层；抓帧边长 ≤ 4096；每层最多 1 个在途抓帧。
- 浏览器崩溃：占位 + 日志；自动重启一次；连续失败禁用该层并提示。
- 导出失败不写占位图，宁可报错。

### 1.8 场景内位置

- `HtmlLayer` 是普通实体，可放进场景文件夹、设父级、被摄像机裁切。
- 变量轨随实体走；场景退休时整层一起退休。

## 2. MCP

### 2.1 服务器与启用

设置 → MCP：

- 本次启动启用（默认关，重启不自动开）；
- 监听 `127.0.0.1`，端口自动或手动；
- 随机 Bearer token，可复制、可重置；
- 权限模式：只读 / 允许修改；
- 日志窗开关；
- “复制 OpenCode / DeepSeek harness 配置”；
- stdio 桥配置说明。

- 启用后写 `~/.config/PVForge/mcp-endpoint.json`（0600）供桥接进程读取；停止时删除。
- 只接受 loopback、Bearer token、JSON-RPC；拒绝非本机、无 token、超大请求。
- 支持 `initialize`、`tools/list`、`tools/call`、`resources/list`、`resources/read`、`notifications/cancelled`、`ping`。
- 传输：Streamable HTTP（POST + 可选 SSE）；只支持 stdio 的客户端用 `pvforge-mcp` 桥接进程转 HTTP。
- OpenCode / DeepSeek harness / Claude Desktop 的实测配置、传输矩阵和验证步骤见 [M6_MCP_CLIENTS.md](M6_MCP_CLIENTS.md)。

### 2.2 工具边界

只读：

| 工具 | 作用 |
|---|---|
| `project.summary` | 尺寸、帧率、时长、活动摄像机、场景列表 |
| `scene.list` / `scene.context` | 场景列表；当前场景、根文件夹、注释、实体 |
| `timeline.get` | 实体、轨道、关键帧（分页） |
| `entity.get` | 单个实体完整状态 |
| `annotations.list` | 全局标记 + 关键帧备注 |
| `assets.list` / `html.get` | 素材、HTML 资产与变量 schema |
| `preview.render_frame` | 指定帧 PNG + 场景摘要（限流） |
| `guide.get` | 2D 工作流指南、HTML 契约、示例 |

写入（仅“允许修改”+ 当前场景）：

| 工具 | 作用 |
|---|---|
| `entity.create` / `entity.update` | 建/改实体、data、变换 |
| `entity.move` / `folder.create` | 移动、放文件夹 |
| `keyframes.set` / `easing.set` | 关键帧与缓动 |
| `entities.retire` | 透明度 0 + 移入 `_retired` + 锁定 |
| `html.create_layer` / `html.set_variables` | 建 HTML 层、改变量 |
| `variables.set_keyframes` | 变量关键帧 |
| `assets.import_blob` | base64/blob + 元数据，无路径 |
| `scene.create` | 只允许追加到末尾，不重叠 |
| `batch.run` | 一组语义操作，原子提交 |

永久禁止：删除实体/资产、任意文件路径、执行命令、导出 MP4、改设置、开关 MCP、写注释。

### 2.3 权限、场景边界与限额

- 默认只读；人类在日志窗或设置里点“允许修改”并确认。
- AI 写入范围：当前播放头所在场景的根文件夹；全局对象（活动摄像机、音频、全局台标）需显式工具/人类解锁。
- 越界返回 `permission_denied`，并提示当前场景。
- 每次调用原子；`batch.run` 整批一个 checkpoint/一条撤销。
- 限额：单批 ≤ 200 操作、工程实体 ≤ 5000、预览截图 ≤ 2 次/秒、单 blob ≤ 5MB。
- AI 连续失败/超时达到阈值时自动停止并提示人类。

### 2.4 急停

入口：

- 日志窗红色“停止 AI”；
- 视口右上角浮动红标“AI 正在操作 · 停止”；
- 全局快捷键（建议 `Ctrl+Shift+.`）。

行为：

1. 中止当前工具调用和浏览器抓帧；
2. 清空排队；
3. 恢复本批次开始快照；
4. 当前调用返回 `stopped_by_human`，并发送 MCP 取消通知；
5. 服务端进入“已急停”，写操作全部拒绝，读操作保留；
6. 人类点“允许 AI 操作”后重新武装。

AI 必须区分“参数错误”“权限拒绝”“人类急停”，不得自动重试急停。

### 2.5 日志窗

复用监视器的停靠模式：

- 默认停靠在视口左侧，可拖出成独立窗口、可关闭；
- MCP 启用但无操作时隐藏；第一次 AI 调用自动展开；
- 关闭后状态栏保留“AI 编辑中 · 停止”入口；
- 每行：时间、工具、目标实体/场景/帧、状态（等待/运行/成功/失败/急停）、耗时；
- 展开：JSON 参数/结果、before/after 差异、渲染缩略图；
- 点击行：选中目标实体、跳播放头、视口闪轮廓；
- 头部：连接状态、权限模式、停止 AI、撤销本批；
- 日志默认只存在内存；可选“导出日志 JSONL”。

### 2.6 AI 的 2D 工作流指南

MCP 资源 `pvforge://guide/authoring`：

- 画布是 2D 像素坐标，原点中心，y 向下；
- 时间轴首行 = 最前，新实体落底部；
- 变换 = 位置、旋转、轴心、缩放、父级链；
- 相机是画布内的 2D 取景框，没有 z 轴、没有 3D 物体；
- 关键帧缓动属于“到达的关键帧”；
- 场景不重叠、可留空；AI 只写当前场景；
- 退休 = 透明 0 + `_retired` 文件夹 + 锁定，永不删除；
- 先读注释和场景上下文，再动手；改完必须 `preview.render_frame` 自查；
- HTML 组件契约与确定性规则。

示例资源：进度条、字幕条、卡拉 OK 高亮。

完整工作流指南见 [AI_AUTHORING.md](AI_AUTHORING.md)；HTML 契约、`pvf` API 与确定性规则见 [M6_HTML_API.md](M6_HTML_API.md)；可直接导入的示例资产在 [examples/html](../examples/html/)。

### 2.7 Checkpoint / git

- 位置：`~/.cache/PVForge/checkpoints/<工程路径哈希>.git`，不碰用户仓库，不污染工程目录。
- 时机：AI 批次开始/结束、危险操作前后；保留最近 100 个。
- 内容：`.pvproj` JSON；素材本身内容寻址，已在工程目录，checkpoint 只记录引用。
- 恢复：文件菜单“恢复检查点”列表（时间、AI 会话摘要、涉及场景）；恢复前自动再存一个“恢复前”checkpoint；恢复只替换工程状态，不动当前素材文件。
- git 不可用：回退为快照文件（同样列表、同样保留数）。

## 3. 场景与注释

### 3.1 场景

- `Scene`：id、名称、起始帧、结束帧、根文件夹 id、颜色、备注。
- 时间轴顶部显示场景带；点击选中，当前播放头所在场景高亮。
- 不重叠；允许中间留空；按起始帧排序。
- 新建实体时，若有活动场景且未指定父级，默认放进场景根文件夹。
- AI：`scene.context` 返回当前场景/根文件夹/区间/注释/实体；写操作默认限制在该文件夹；`scene.create` 只允许追加到末尾。
- 场景退休：文件夹移入 `_retired/<场景名>`；透明度 0；锁定；数据和注释保留；人类可恢复，AI 不可恢复。
- v3 工程迁移：创建默认场景覆盖 0..timelineLength，把根实体收进去，避免 AI 无上下文。

### 3.2 注释

- 全局标记轨：帧号、标题、Markdown、标签（字幕/场景/副歌/镜头）、颜色；显示在刻度尺下方；点击跳转。
- 关键帧备注：右键“备注…”；有关键帧显示小点，悬停显示；不参与渲染。
- 场景备注偏“这一段是什么”，标记偏“这一帧发生了什么”。
- MCP 只读；人类写。
- 另提供 `audio.summary`（时长、开始/裁剪、波形峰值摘要），不做 BPM 分析。

## 4. 工程格式 v4

- 现状 `version: 3`；新增字段后升到 4。
- 新字段：`scenes`、`annotations`、`HtmlLayer` 实体、关键帧可选 `note`、`var.*` 轨道。
- 加载器：接受 1–4；v1–v3 补默认场景、空注释；版本 > 4 明确报错。
- HTML 资产：`assets/html/<sha>.json`；打包开启时随工程复制。
- 保存仍用 `QSaveFile` 原子写入。

## 5. 实施阶段

粗估按单人全职。

### Stage 0：浏览器/CDP spike（已完成，NO-GO at 25ms）

- 外部 Chrome/Edge headless：1280×720 / 1920×1080 连续 100 帧；变量注入 → rendered → 截图 → 解码；透明背景、字体、内联图片；多页面、内存、崩溃重启；断网、无 file 访问；同一帧两次像素哈希。
- 结果（Chrome 157，100 帧中位）：720p 总 61.23ms（截图 43.10ms，等效 16.3fps）；1080p 总 86.97ms（截图 53.51ms，等效 11.5fps）；双页面 720p 每轮 150ms、1080p 188ms；2 页 Chrome 进程树 RSS 约 1.6–1.7GiB。
- 门槛 25ms **未达到**。按降级策略执行：
  - 预览按显示尺寸抓（通常 960 宽以内），15fps 上限；1080p 不承诺 30fps。
  - 导出不节流、全分辨率逐帧等待；1080p HTML 层本身约 11–16fps，长工程会比实时慢，属于可接受成本。
  - 多 HTML 层串行 `bringToFront` 抓帧；RSS 高，v1 最多 2 层。
- 关键实现发现：headless 只为前台标签页合成，每次截图前必须 `Page.bringToFront`/`Target.activateTarget`；`Page.addScriptToEvaluateOnNewDocument` 对 `Page.setDocumentContent` 不生效，确定性 stub 必须内联到文档第一个 `<script>`。
- 产出：`docs/AI/M6_SPIKE.md` 测量报告 + go/no-go；spike 不集成进主程序。

### Stage 1：HTML 层核心（约 2 周）

- Project v4 字段、`HtmlLayer`、资产存储、浏览器管理、CDP、缓存、SceneRenderer/视口/监视器合成；
- 变量轨、属性面板、素材库、缩略图、替换；
- 异步 `VideoExporter` + HTML 导出；
- 测试：`--html-capture-test`、`--html-variables-test`、`--html-export-test`、`--html-determinism-test`。

### Stage 2：场景与注释（约 1 周）

- Scene 数据、时间轴场景带、活动场景、实体落点、退休/恢复；
- 全局标记、关键帧备注、MCP 只读接口；
- 测试：`--scene-test`、`--annotation-test`。

### Stage 3：MCP 只读 + 日志窗（约 1.5 周）

- HTTP 服务、token、设置页、stdio 桥、只读工具、指南资源、`render_preview`；
- 日志窗停靠/弹出/关闭、行详情、跳转高亮、连接状态；
- 测试：`--mcp-readonly-test`、`--mcp-log-test`。

### Stage 4：MCP 写入 + 安全（约 2 周）

- 写工具、`batch.run`、只读默认、显式武装、场景边界、退休、限额；
- 急停、批次快照、隐藏 git checkpoint、恢复列表；
- 测试：`--mcp-edit-test`、`--mcp-stop-test`、`--mcp-checkpoint-test`。

### Stage 5：硬化与文档（约 1 周）

- `docs/AI/AI_AUTHORING.md`、HTML 示例（进度条/字幕条/卡拉 OK）、FEATURES/USAGE/MILESTONE 更新；
- 性能、内存、长会话、浏览器崩溃、MCP 客户端兼容；
- 全量自检（现有 52 + 新增）。

总计约 7–8 周单人；Stage 1 和 Stage 3 可拆给不同人并行。

## 6. 安全基线（P0）

完整威胁清单见 [M6_SECURITY.md](M6_SECURITY.md)。上线前必须满足：

- **CDP 传输**：生产用 `--remote-debugging-pipe`（fd 3/4，null 分隔 JSON），不用 `--remote-debugging-port`、不用 `--remote-allow-origins=*`；端口方案只允许出现在本地 spike。
- **HTML 隔离**：组件跑在 sandbox iframe/隔离世界；CSP `connect-src 'none'`，img/font 只允许 data:，禁 media/frame/worker/下载/弹窗/剪贴板。
- **断网多层**：CDP 拦截 + CSP + 禁 WebSocket/WebRTC/代理环境变量；fetch/WebSocket/图片 canary 必须失败，探针失败则禁用 HTML。
- **WebGL/file 探针**：`--disable-3d-apis`；页面 `getContext('webgl')===null`；`file://` 必须失败。
- **MCP HTTP**：Host/Origin/CORS 校验，Bearer 常量时间比较，header/body/连接/超时限额，拒绝 Content-Length+Transfer-Encoding。
- **工程加载**：文件 64 MiB、实体 5000、关键帧 200k、字符串 1 MiB 上限；版本 >4 拒绝；未知类型拒绝；相对路径不得逃逸工程目录；资产哈希与内容一致。
- **场景边界**：执行时按目标实体根祖先复核解锁时快照的 sceneRootId，播放头移动不能扩大 AI 范围。
- **急停**：epoch + 每 op/commit 前检查；禁止事务中嵌套事件循环；提交前回滚、提交后明确 `stopped_after_commit`。
- **checkpoint**：写入前 checkpoint 必须成功；project UUID + sidecar SHA-256；git plumbing-only、禁 hook、隔离全局配置。
- **资源看门狗**：Chrome 进程组、CDP/页面/截图超时、CPU/RSS/像素总预算、并发 page 上限。

安全验收测试：`--html-canary-test`、`--mcp-http-test`、`--mcp-scope-test`、`--mcp-stop-race-test`、`--project-limit-test`、`--untrusted-project-test`。

## 7. 风险与对策

| 风险 | 对策 |
|---|---|
| 用户机器没有 Chrome/Edge | 自动探测 + 手动路径 + 禁用提示；文档说明依赖 |
| CDP 截图慢，预览掉帧 | 预览按显示尺寸抓 + 15fps 上限 + 单在途请求；导出不节流 |
| Chrome 更新/CDP 变化 | 只用稳定域（Page/Runtime/Emulation）；同时支持 Edge |
| 字体/资源导致不确定性 | 全部内联 + `document.fonts.ready` + 确定性时钟/随机 |
| HTML 组件写死动画 | 注入禁用 CSS 动画/transition；超时失败；同帧哈希测试 |
| AI 炸工程 | 只读默认、场景边界、无删除、批次快照、急停、checkpoint、限额 |
| 急停后 AI 继续重试 | 结构化 `stopped_by_human` + 服务端 stopped 状态 + 取消通知 |
| 检查点提交开销 | 批次级提交、内容寻址素材、只保留 100 个 |
| 工程膨胀 | 退休文件夹 + 后续归档/清理功能；注释说明 |
| MCP 客户端只支持 stdio | HTTP 主 + `pvforge-mcp` 桥；配置一键复制 |
| 工程格式不兼容 | version 4；旧版加载补默认；新版拒绝旧应用并明确提示 |

## 7. 测试清单

现有 52 个自检必须保持通过，新增：

- `--html-capture-test`：浏览器缺失时跳过；抓帧尺寸/颜色/透明。
- `--html-determinism-test`：同帧两次像素哈希一致。
- `--html-variables-test`：变量/关键帧/缓动求值。
- `--html-export-test`：带 HTML 层的 1 秒 MP4，解码验色。
- `--scene-test`：场景不重叠、落点、退休。
- `--annotation-test`：标记/关键帧备注、跳转、AI 只读。
- `--mcp-readonly-test`：initialize/tools/list/读工具/写拒绝。
- `--mcp-edit-test`：建实体/关键帧/HTML 变量、批次原子。
- `--mcp-stop-test`：急停回滚 + `stopped_by_human` + 重新武装。
- `--mcp-checkpoint-test`：批次 checkpoint、恢复列表。
- `--async-export-test`：异步帧供应器取消/超时/进度。

## 8. 计划默认（可改，不阻塞）

- 预览默认 15fps（1080p 全质量抓帧只有约 11.5fps，30fps 不现实）。
- HTML 资产用单个 JSON（html + manifest），不是两个文件。
- 最多 2 个 HTML 层、16 个组件、2MB HTML、5MB 资产、4096 边长。
- AI 可以 `scene.create`，但只能追加到末尾；恢复场景只能人类做。
- checkpoint 放 `~/.cache/PVForge/checkpoints/`，不碰工程目录和用户仓库。
- 日志只存内存，可选导出 JSONL。
- v1 不做 WebGL/视频/音频组件，不做人类代码编辑器。

## 9. 依赖变化

- 生产 CDP 走 `--remote-debugging-pipe`，不依赖 Qt WebSockets；如果后续必须用 loopback WebSocket，再单独评估。
- Stage 0 spike 可先用 Node.js + 内置 WebSocket 验证抓帧延迟，不阻塞主程序依赖。
- 外部 Chrome/Edge 是运行依赖，不随应用分发。

## 10. 非目标

- 不做 QtWebEngine 内置后端（接口预留）。
- 不做节点式合成、3D、Minecraft 资产、多轨视频剪辑。
- 不做人类 HTML 代码编辑器；组件按素材替换。
- 不做 AI 自主导出 MP4。
