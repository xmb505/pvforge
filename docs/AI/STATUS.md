> **AI 文档（非用户文档）**：本文由 AI/代理生成，面向 AI 与开发者，可能滞后于代码。用户请以 `README.md`、`docs/USAGE.md`、`docs/FEATURES.md` 为准。

# PVForge 新模型状态与交接

<!-- metrics:begin -->
<!-- 本区块由 `bash tools/doc-metrics.sh --write` 生成，请勿手改；CI 用 `--check` 校验。 -->
| 指标 | 值 |
| --- | --- |
| 生成日期 | 2026-11-13 |
| HEAD | `bdf81a0` |
| `src/main.cpp` 行数 | 128 |
| `src/tests` 行数 | 25279 |
| 测试源码行数（含模块 tests） | 63056 |
| 非测试源码行数 | 124278 |
| `--*-test` 自检开关数 | 95 |
| CTest 用例数 | 330 |
| `dist/pvforge` 大小 | 6.4 MiB（6715992 字节） |
| Release 动态库依赖数 | 175（ldd 动态库依赖） |
<!-- metrics:end -->

> 本文件的指标（HEAD、源码行数、测试数、二进制大小）见上方指标区块，由 `tools/doc-metrics.sh` 生成；正文不再手抄这些数字。  
> **2026-11-10 方向修正**：交付壳是旧 UI（`src/app` + `src/ui`），新后端模块逐步接入旧壳；`src/next/app` / `pvforge-next` 只作参考，不交付。旧 app 不删除；产品名只有 `PVForge`。基线 tag：`legacy-ui-baseline`（`ba07a59`）。本文以下历史描述以本段为准。  
> **2026-11-11 旧 UI 接入完成**：P1–P7 已合并；最终二进制 `dist/pvforge` 与 `dist/pvforge-mcp`（大小见顶部指标区块）。旧 UI 的时间轴/属性/视口/Gizmo/相机监视器/素材箱/MCP/设置/导出均已接新后端。  
> 设计主文档：`docs/AI/ENTITY_MODEL.md`  
> 并行实现合同：`docs/AI/IMPLEMENTATION_CONTRACT.md`  
> 整合计划：`docs/AI/INTEGRATION_PLAN.md`  
> MCP 工具文档：`docs/AI/MCP_TOOLS.md`

## 1. 当前结论

新模型不是旧工程的迁移，而是一套平行实现：

- 旧 `pvforge` 目标就是交付壳，保留 `src/app` + `src/ui` 的布局、面板、快捷键和交互；新后端逐步接入。（2026-11-10 方向修正：原“作为参考”改为“交付壳”。）
- `src/model`、`store`、`assets`、`solver`、`anim`、`render2`、`html2`、`htmlcommon`、`framecache`、`video2`、`audio2`、`export2`、`mcp2` 是新后端；`pvforge-next` / `src/next/app` 只作行为参考，不交付、不改名替代。
- 旧工程 `.pvproj` 单文件不读取；新后端目标是 `.pvproj/` 目录。迁移期旧壳仍读写旧单文件，P1 后切换。
- 旧场景/文件夹/clip/模板模型在新后端全部替换；迁移期旧壳界面暂保留这些旧结构，不因接后端提前删除。

## 2. 已定设计（速查）

### 实体

- 时间轴上的对象叫实体（Entity）。
- 实体字段：UUID、名字（可重名，默认素材名）、start/end（全局帧、半开区间）、row（每个父级独立）、parentUuid、hidden/locked/visible/inheritVisibility/physicsInteract、transform、content、keyframes、solvers。
- 任意实体可当父级；子实体位置/旋转/缩放是局部坐标并继承父变换；子实体时间存绝对帧。
- 父实体在时间上移动时，后代 start/end 同步偏移；父级裁剪级联裁剪/删除后代。
- 空实体 = 没有内容的普通实体，可以当容器，也可以挂素材。
- 父级裁剪、删除父级、扩大范围不恢复已删内容。
- 换父级每次询问“保持世界位置 / 保持局部坐标”；不能形成父子环，子范围必须落在新父范围内。
- 所有实体允许负 scale，包括摄像机。

### 时间轴

- 无限长；导出范围由选定摄像机决定。
- PR 式从下往上：row 0 在最底，row 越大越靠前；新实体默认放在整个 `[start,end)` 范围最高占用 row + 1。
- 私有时间线继续用浮层弹窗；主时间轴双击实体条 = 在双击帧打 key；“+”按钮 = 打开私有时间线。
- 实体第一版单选；关键帧支持 Ctrl/Shift 多选、拖动、Del、Ctrl+C/V（同实体同轨道）。
- 合并菱形：同一帧所有轨道 key 一起移动。
- markers 全局保留。

### 关键帧

- 帧号相对实体 start，整数，只允许 `0..duration-1`。
- 移动实体不改关键帧；裁剪 start 时 key 平移/删除，sourceIn 同步调整；裁剪 end 删除越界 key。
- Auto-key 默认开启；Properties 有关键帧区、菱形按钮、缓动预设 + 自定义贝塞尔。
- 求解段：一个实体一条时间轴，段不能重叠；一个段一个效果（gravity/orbit/spin/blackhole/spiral）。
- 开始帧必须是已有普通关键帧，人类把它“转换为求解开始帧”；不自动创建 key。
- 结束帧只是停止标记；段内被驱动属性不能有普通 key；段后普通 key 从解算结束位姿继续。
- 裁剪 start 切进段：删段并把新起点解算位姿落成普通 key；裁剪 end 切进段：结束标记冻结在新末帧。
- 求解轨迹预览 + 初速度手柄在 Properties。

### 素材

- 素材没有 UUID，只有 sha256；实体引用 assetHash。
- 文本/矢量是实体元数据，不进素材箱；HTML 是 `.pvhtml` 目录包素材；图片/视频/音频是素材。
- 素材文件在工程 `assets/`，但**不进 git**；git 只管理文本。
- 素材同 hash 去重，保留第一次导入名；可改名、打标签。
- 换素材按兼容组：视觉素材（图/视频/HTML）、生成内容（文字/矢量）、音频、摄像机；空实体可接任意组。
- 换短素材自动裁实体 end，越界 key 删除。
- 删除被引用素材不阻止，实体显示“素材缺失”，重新导入同内容恢复。
- 视频导入自动拆视频实体 + 独立 WAV 音频实体；之后完全不联动。
- 视频导入后**后台按项目 fps 全量抽 WebP 80 图片序列**，放工程 `.cache/video/<assetHash>/<fps>/`；像素 hash 全局去重；LRU 淘汰后回退原片解码；导出用原片。

### HTML

- `.pvhtml` 目录包（index.html + 资源），可导入 zip。
- 三个 Chrome：前端低清（常驻）、缓存低清（常驻）、成品高清（导出时懒启动）。
- 缓存键：assetHash + HTML 本地帧/时间 + 变量值 + 输出尺寸 + Chrome 版本 + bundle 入口 hash；与时间轴位置无关。
- `timeDependent=false` 只缓存一张；true 按帧/变量。
- 低清：工程分辨率比例（25/50/75/100%，默认 50%），宽度上限 1280，WebP 80。
- 缓存：进程内 QImage LRU（默认 256MB）+ 本地磁盘 WebP LRU（默认 2GB）；**无 Redis/cache-cat/MySQL**。
- 预取：当前帧之后到下一个关键帧/变量变化/片段末尾，再向前。
- 当前帧 miss：最多同步等 300ms；超时显示“HTML 渲染中”，后台继续。
- 看门狗：异步 QTimer，只监控播放；每真正上屏一帧喂狗；播放中超过阈值（默认 3000ms，可设）暂停；暂停时狗睡觉；手动恢复。
- Chrome 升级：版本进 key；按资产抽样 3 帧比 sha256，不一致只清该资产缓存。
- `.pvhtml` 页面协议：`window.__PVFORGE__ = {frame,time,vars}`、`pvforgeframe` 事件、可选 `__PVFORGE_READY__`。

### Git / Undo

- `git` 硬依赖；**不再依赖 git-lfs**。
- 只跟踪 `project.json`、`entities/*.json`、markers 等文本；`assets/`、`.cache/` gitignore。
- AI 每次 MCP 写工具调用立即 commit（冷备，防 AI 改炸）。
- 人类操作走内存 undo；停手 1 秒/手动保存时合并成一个 commit。
- Ctrl+Z/Ctrl+Shift+Z 是**内存快照 undo/redo**，不调用 git revert、不产生 commit；覆盖文本数据，不含素材文件；默认最多 200 步或 512MB。
- 应用关闭后内存栈丢失，靠 git 历史冷恢复。
- 打开工程检测未提交文本，提供恢复/丢弃/提交（Store API 已有，UI 待接）。

### 渲染 / 导出

- 渲染顺序：root 按 row 升序；父内容先画，子按 row 升序。
- 变换：`localToWorld(p) = (x,y)+pivot+R·(S·(p-pivot))`；Gizmo/渲染/MCP 共用。
- 导出范围 = 选定摄像机 `[start,end)`；无摄像机报错。
- 输出 fps 默认工程 fps，可选 2x/4x；关键帧/视频按时间采样，音频重采样。
- 输出分辨率默认工程分辨率，可选 1x/2x/4x 整数倍。
- 首版导出混音频：活跃音频实体按 start/end + sourceIn + volume 混合，AAC；缺失跳过。
- 视频导出用原片，不用图片序列；HTML 导出用成品浏览器高清逐帧。

### MCP

- 46 个工具（Phase A + B）。
- HTTP loopback + Bearer token；端口默认 47821，token 首次生成后持久化，重启不换；端口冲突报错不静默换。
- AI 默认可写全工程；危险操作 `confirm: true`。
- AI 写操作期间锁人类编辑；只读不锁。
- MCP 日志放在相机监视器旁的浮窗（2026-11-10 方向修正：按 `USER_REQUIREMENTS.md` 和旧 `MainWindow::arrangeMcpLog()`，不是底部 dock），显示工具名、参数摘要、耗时、结果、commit；有急停按钮。急停取消当前调用并暂停 AI 写入，直到人类恢复；AI 收到 `stopped_by_human`“人类按下急停，此次操作未实现”。
- 一次写工具调用一个 commit；纯素材导入可能没有 commit（素材不进 git）。
- undo/redo 走内存，不产生 commit；git 历史是冷恢复。
- 预览返回当前帧 PNG 路径 + UI 状态 + 可选摄像机。

### 设置

- 功能设置全局（QSettings）：HTML 清晰度/缓存预算/看门狗、Chrome 路径、视频代理、MCP 端口/token、人类 commit debounce。
- 工程设置单工程：fps、分辨率、背景色、markers。
- fps/分辨率创建后锁定，渲染页可另设输出 fps/分辨率。
- UI 沿用旧 pvforge 深色主题；实体第一版单选。

## 3. 已实现模块

测试数量**不在这里手写**：唯一来源是 CMake 注册的 CTest 用例（模块或根 `CMakeLists.txt`；`ctest --test-dir <build> -N` 查看），总数见顶部指标区块。下表只描述模块职责与状态。

| 模块 | 目录 | 状态 / 职责 |
|---|---|---|
| Model | `src/model` | JSON/校验/小数帧求值 |
| Solver | `src/solver` | 重力/公转/自转/黑洞 |
| Assets | `src/assets` | sha256/素材库/WAV/.pvhtml/标签 |
| Gizmo | `src/gizmo` | 四角旋转/Office 缩放/中心四象限 |
| Store | `src/store` | 文本-only git（可关闭）、内存 undo、debounce、崩溃恢复、恢复到 commit |
| Anim | `src/anim` | 普通/求解段/父子/可见性/小数帧 |
| Renderer2 | `src/render2` | 图/矢量/文字/相机/素材丢失卡片/provider |
| Export2 | `src/export2` | MP4 + AAC 音频、2x/4x fps、1x/2x/4x 分辨率 |
| Audio2 | `src/audio2` | 混音 + QAudioSink 播放 |
| Frame cache | `src/framecache` | 共用 SQLite 帧缓存 schema/目录 LRU/解码图 LRU；`workbench` 与 `htmlcache` 共用 |
| HTML cache | `src/htmlcache` | 内存 QImage LRU + 磁盘 WebP LRU（帧缓存底层走 `src/framecache`） |
| HTML common | `src/htmlcommon` | CDP 传输（pipe/WebSocket）/`CdpClient`/`HtmlBrowser`；无独立 CMake 目标，源码由 `html2` 与交付目标直接编译 |
| HTML2 | `src/html2` | `.pvhtml` 本地服务器/Chrome/三浏览器/异步/8192 上限 |
| Video2 | `src/video2` | 取帧/代理/全量抽图/队列/设置 |
| MCP2 | `src/mcp2` | 46 工具/HTTP/急停/预览 provider/无 Git 只读 |
| Workbench | `src/workbench` | 帧元信息/整帧 WebP/SQLite LRU/预渲染扫描 |
| Next core | `src/next/core` | Document 接口 |
| Next app | `src/next/app` | 主窗口/播放看门狗/媒体/导出/崩溃恢复/Git 引导+历史/工作台缓存/Debug（参考壳，不交付） |
| Timeline | `src/next/timeline` | 行/裁剪/关键帧/浮层/标记 CRUD/音频峰值 |
| Properties | `src/next/properties` | 属性/auto-key/求解段/HTML 变量/换素材/33 缓动 |
| AssetPanel | `src/next/assets` | 列表/导入/标签/抽图进度/补抽 |
| Viewport | `src/next/viewport` | 渲染/gizmo/provider/吸附/工作台命中 |
| Settings | `src/next/settings` | 5 页 QSettings/Git 引导/工作台缓存 |
| MCP log | `src/next/mcp` | 日志/急停/恢复 |
| History | `src/next/history` | Git 历史列表/恢复到提交 |

根 CMake 已包含全部新模块目标（`src/htmlcommon` 无独立目标，随 `html2`/交付目标编译）；`pvforge-next` 可链接，但只作参考，不是交付目标（2026-11-10 方向修正）。

### 3.1 端到端验收（2026-11-10）

在 `/tmp/pvfe2e` 生成 7 实体工程（摄像机/文字/矢量/图片/视频/音频/HTML，640×360@30），用 `QT_QPA_PLATFORM=offscreen` 启动 `pvforge-next --open`，通过真实 HTTP MCP 验证：

- `tools/list` 返回 46 个工具；`project.get`/`view.state` 正常。
- `entity.create`、`keyframes.set`、`entity.update`、`solver.create`（gravity）、`markers.set`、`asset.setTags` 成功并各自产生 commit。
- `project.update` 改 fps 被 `conflict` 拒绝。
- `preview.render_frame` 的 PNG 同时包含 HTML 红色层和视频/图片像素（说明 provider 已接入 MCP 预览）。
- `history.undo/redo` 恢复 marker，且不新增 git commit。
- 播放/缓存只有真实帧才喂看门狗；HTML 低清 miss 最多等 300 ms，paint 内只查缓存。

## 4. 当前未完成 / 已知缺口

1. **导出 GUI 未做整链路自动化**：Export2 + provider + 对话框 getter 已测，但“点导出按钮 → 2x/HTML 高清 → 成片”仍靠人工验收。
2. **HTML 8192 实际渲染**：边界校验和 4000×100 实渲染已测；8192×4320 的 Chrome 内存/耗时未压测。
3. **HTML Chrome 升级抽样校验**未实现（版本进 key，未抽样清资产缓存）。
4. **视频导出 4x 8K**：8192 上限已放开，但 7680×4320 的内存/编码速度未压测。
5. **AI 并发写入**：导出期间未阻止 MCP 写操作；export2 用工程快照，MediaServices provider 读实时文档。
6. **内存 undo 不覆盖素材标签/素材文件**：标签在 `assets/*/asset.json`，属于素材元数据；undo/redo 只恢复文本工程。
7. **崩溃恢复扫描白名单**：只认 `project.json`、`entities/*.json`、`.gitignore`；未来新增独立文本文件需扩展。
8. **崩溃恢复弹窗**只用注入 handler 测业务分支，真实模态框未自动化。
9. **Timeline 实体多选/框选**：第一版单选；关键帧多选已做。
10. **旧 app 删除**：~~`pvforge-next` parity 后再删旧 `src/core/Project.*`、旧时间轴/场景/测试。~~（2026-11-10 方向修正：取消。旧 app 是交付壳，不删除；旧模型源码等新后端接入完成后只清理内部实现。）
11. **旧测试**：大量旧测试是旧模型断言，未迁移；新目标测试已独立。
12. **求解段递归守卫**：现在同一实体在求解中重新求值会退回无求解的 plain pose；跨实体循环仍由父链/目标校验拦截。

### 4.0.1 2026-11-11 UI 回归修复（对应 issue #37–#51）

- #44/#37 坐标：画布中心 (0,0)、+Y 向上、正旋转逆时针；render2/anim/gizmo/solver/MCP/导出统一；旧 Viewport 已接（`ad2ea5f`、`c1b8d71`、`49b54d3`）。
- #39 时间轴：row 0 在最底、上下自由拖到任意空白行、删除不压缩（`2a0af9a`）。
- #46/#48/#49/#51 时间轴旧 UI：1/5/10/50/200 标尺+小刻度、名称列/搜索/眼睛锁、默认点菱形选中/拖动、直角扁平片段（`f3efec5`）。
- #40/#42 缓动：同一帧所有轨道共用一次整体缓动，逐轨下拉删除，时间轴缓动入口移除（`2e47caf`）。
- #43/#38 求解：最后一个普通 key 可“转换为求解关键帧”；求解到 N-1，N 帧新 key 是后续起点；删除自动延伸；多段；自动物理停止原因；视口轨迹 + 初速度手柄（`7ca1a2f`、`1d21d43`、`8ffc1d0`、`6cdede6`）。
- #41 Inkscape 四角弯箭头旋转手柄：角上 18px、方形命中、Shift 绕对角、Ctrl 关吸附、负 scale 角对应（`66e4e14`）。
- #45/#47 布局/素材库：时间轴恢复 220px 全宽、素材库回右侧并显示创建/删除、工作台创建可见（`e759f00`）。
- #50 属性：实体/关键帧默认折叠、内容区回首屏、相机悬空菱形删除（`2e47caf`）。

### 4.0 2026-11-11 旧 UI 接入完成

已合并分支 `ui-backend-integration`（P1–P7）：

- P1 旧 MainWindow 打开/新建目录工程，旧布局/项目属性；`1e875d1`
- P2a 旧时间轴只读显示新实体；`7c1bed7`
- P2b1 片段拖动/裁剪/换行/换父级/级联删除；`f19a465`
- P2b2 关键帧/marker/S 切分；`311ab94`
- P3a 旧视口 render2/播放/缓存；`a355f6a`
- P3b Gizmo/吸附/相机监视器/临摹；`d2b3cf5`
- P4a 旧属性面板实体/Content/关键帧/33 缓动/求解；`c0459de`
- P4b 特殊内容/HTML 变量/文字排版/品牌墙；`fce5675`
- P5 旧素材箱/导入/拖拽/换素材；`45d62e2`
- P6 MCP/Git/历史/崩溃恢复/设置；`32def53`
- P7a 旧导出 UI 接 export2（fps/分辨率/CRF/编码器/ETA/取消/覆盖）；`b1dc1f7`
- P7b 移除 `pvforge-next` 交付目标与旧导出管线；`491c270`

最终二进制：`dist/pvforge`（Release）、`dist/pvforge-mcp`；大小见顶部指标区块，启动说明 `dist/RUN.txt`。

验收：`--self-test`、`--session-test`、`--viewport-test`、`--timeline-test`、`--properties-test`、`--assets-test`、`--mcp-test`、`--git-test`、`--export-test` 全部通过；`--open /tmp/pvfe2e/proj` 启动不崩；`ldd` 无缺失。

已知剩余：

1. 旧栈更广清理（History/Commands/CheckpointManager、SceneRenderer/CameraRenderer、VideoPlayer/VideoProxy、HtmlRenderer/McpServer）未做，旧自测仍在用。
2. `src/next/app` 参考壳内部仍有 `PVForge Next` 文案，但已无交付目标、不进入 `pvforge`。
3. `htmlCacheEnabled` / `videoProxyEnabled` 只持久化显示，底层关闭缓存入口未接。
4. 真实人工验收：2x/HTML 高清/VAAPI/4x 8K 压力、打开文件夹按钮。
5. 旧 `--timeline-frame-test`/`--timeline-interaction-test`/`--timeline-rowbuttons-test` 是基线既有失败，未修。

### 4.1 2026-11-10 晚间进展

已提交：

- Git 可选模式（默认关、找不到二进制回退）、首次引导、历史面板/恢复、崩溃恢复。
- 工作台缓存：元信息比较、每相机、50%/8GB、扩展 50% 透明底、预取、Debug、HTML 元信息、内容轨道。
- MCP：契约对齐、只读/急停/串行 busy、写后 UI 刷新、AI 锁、外部素材 confirm、历史恢复。
- 时间轴：播放头常驻、固定行、一次手势一步 undo、刻度居中、S 切分、收尾吸附、标记 CRUD、音频峰值。
- Properties：HTML 变量、换素材、文字排版、33 缓动、特殊内容编辑（漫画/线条/点阵/品牌墙）。
- 相机监视器 + 临摹视频（无串流）；MCP 日志在监视器旁（顶栏 dock）。
- 渲染：素材丢失卡片、内容轨道动画、品牌墙、扩展缓存透明底。
- 导出：可见音频、覆盖确认、4x 大尺寸确认；HTML 预取 + 真实上屏看门狗。
- 根 CTest 注册（数量见顶部指标区块）。

仍未完成：

1. 自动保存 5 分钟、退出保存播放头、标题未保存/已保存。
2. 导入“+”识别所有文件、素材自动打包开关（默认关）。
3. 导出 CRF 档位、编码器（VAAPI）、ETA、导出前预览。
4. 特殊内容创建入口（菜单/工具栏）与 MCP schema/update 支持新 kind。
5. 工作台后台预热已取消（产品决策：按需缓存、不预热）；`PrewarmService` 已删除，`WorkbenchServices` 在 UI 线程按需扫描；文档 3 秒 vs 代码 500ms。
6. 相机监视器 dock 几何不持久化；参考视频同步解码。
7. 旧 app 删除/改名（2026-11-10 方向修正：取消；旧 app 原地保留，只替换内部实现；先迁移 core 文字/矢量与 html CDP 共享件仍有效）。
8. 品牌墙为文字标签版，旧图片/SVG 贴图未迁；Properties 无关键帧轨道。

## 5. 构建 / 测试

交付目标是旧壳 `pvforge`（`src/app` + `src/ui` + 新后端）；`pvforge-next` 只作参考，不交付、不安装。2026-11-10 方向修正后，构建文档以本节为准。

```bash
# 交付目标（旧壳）
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target pvforge -j

# 参考目标（不交付）
cmake --build build --target pvforge-next -j

# 运行（示例）
./build/pvforge
./build/src/next/app/pvforge-next --open /path/to/foo.pvproj  # 仅参考

# 模块独立测试（推荐；根 ctest 可能未注册顶层测试）
cmake -S src/store -B /tmp/pvf-store-build -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/pvf-store-build -j
ctest --test-dir /tmp/pvf-store-build --output-on-failure
```

环境：

- Linux，Qt 6.10.2，FFmpeg libav* 可用。
- Chrome：`/usr/bin/google-chrome`，Google Chrome 157.0.8103.38。
- git 已安装；**不需要 git-lfs**。
- 音频设备在无头测试中不可用，Audio2 会优雅失败。

## 6. 正在进行 / 交接注意

- 当前工作区可能有 agent 未提交的改动；交接前先 `git status`。
- 不要提交正在跑的 agent 半成品；按目录 review 后再 commit。
- 旧分支 `timeline-clip-merge` 保留，不合并；当前工作分支 `new-entity-model`。
- 文档索引：
  - `ENTITY_MODEL.md`：新模型权威设计
  - `MCP_TOOLS.md`：MCP 46 工具 schema（已按 text-only git / memory undo 更新）
  - `ISSUE_TRIAGE_A/B/NEW.md`：旧 issue 分诊/新模型风险
  - `ISSUE_DECISIONS.md`：issue 处理决策
  - `IMPLEMENTATION_CONTRACT.md`：模块接口合同
  - `INTEGRATION_PLAN.md`：整合顺序（部分历史阶段描述已过时）

## 7. 下一步建议顺序

详见 [ROADMAP.md](ROADMAP.md)。

1. 完成 Git 可选模式的主窗口接线（引导/设置/历史面板/MCP 只读/导出开文件夹）。
2. 第一批低成本项：项目属性/背景色、更换素材、快捷键、吸附、HTML 变量、marker CRUD、音频峰值。
3. 第二批：30 种缓动、文字排版、导出 CRF/编码器/ETA/预览、音频淡入淡出。
4. 第三批：漫画框/线条/点阵/品牌墙作为一等公民。
5. 第四批：相机监视器（无串流）+ 临摹 + 实体多选。
6. ~~最后删除旧 app/旧模型/旧测试，`pvforge-next` 改名 `pvforge`。~~（2026-11-10 方向修正：取消。交付目标始终是旧壳 `pvforge`，`pvforge-next` 只作参考。）
