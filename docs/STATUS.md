# PVForge 新模型状态与交接

> 日期：2026-11-10  
> 分支：`new-entity-model`  
> 最新已提交：`2ba02cc feat: pause playback when html frames stall`  
> 设计主文档：`docs/ENTITY_MODEL.md`  
> 并行实现合同：`docs/IMPLEMENTATION_CONTRACT.md`  
> 整合计划：`docs/INTEGRATION_PLAN.md`  
> MCP 工具文档：`docs/MCP_TOOLS.md`

## 1. 当前结论

新模型不是旧工程的迁移，而是一套平行实现：

- 旧 `pvforge` 目标保留可构建，作为参考。
- 新目标 `pvforge-next` 使用 `src/model`、`store`、`assets`、`solver`、`anim`、`render2`、`html2`、`video2`、`audio2`、`export2`、`mcp2`、`next/*`。
- 旧工程 `.pvproj` 单文件不读取；新工程是 `.pvproj/` 目录。
- 旧场景/文件夹/clip/模板模型全部替换；没有场景对象。

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
- 求解段：一个实体一条时间轴，段不能重叠；一个段一个效果（gravity/orbit/spin/blackhole）。
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
- MCP 日志底部 dock（旧面板风格）；急停取消当前调用并暂停 AI 写入，直到人类恢复；AI 收到 `stopped_by_human`“人类按下急停，此次操作未实现”。
- 一次写工具调用一个 commit；纯素材导入可能没有 commit（素材不进 git）。
- undo/redo 走内存，不产生 commit；git 历史是冷恢复。
- 预览返回当前帧 PNG 路径 + UI 状态 + 可选摄像机。

### 设置

- 功能设置全局（QSettings）：HTML 清晰度/缓存预算/看门狗、Chrome 路径、视频代理、MCP 端口/token、人类 commit debounce。
- 工程设置单工程：fps、分辨率、背景色、markers。
- fps/分辨率创建后锁定，渲染页可另设输出 fps/分辨率。
- UI 沿用旧 pvforge 深色主题；实体第一版单选。

## 3. 已实现模块

| 模块 | 目录 | 状态 / 测试 |
|---|---|---|
| Model | `src/model` | JSON/校验/小数帧求值；14 组测试 |
| Solver | `src/solver` | 重力/公转/自转/黑洞；10 测试 |
| Assets | `src/assets` | sha256/素材库/WAV/.pvhtml/标签；17 CTest |
| Gizmo | `src/gizmo` | 四角旋转/Office 缩放/中心四象限；10 组 265 断言 |
| Store | `src/store` | 文本-only git、内存 undo、debounce、崩溃恢复扫描；21 测试 |
| Anim | `src/anim` | 普通/求解段/父子/可见性/小数帧；12 测试 |
| Renderer2 | `src/render2` | 图/矢量/文字/相机/占位/provider；11 测试 |
| Export2 | `src/export2` | MP4 + AAC 音频、2x/4x fps、1x/2x/4x 分辨率；10 CTest |
| Audio2 | `src/audio2` | 混音 + QAudioSink 播放；8 测试 |
| HTML cache | `src/htmlcache` | 内存 QImage LRU + 磁盘 WebP LRU；13 测试 |
| HTML2 | `src/html2` | `.pvhtml` 本地服务器/Chrome/三浏览器/异步；14 测试 |
| Video2 | `src/video2` | 取帧/代理/全量抽图/队列；25 测试 |
| MCP2 | `src/mcp2` | 46 工具/HTTP/急停/预览；40 测试 |
| Next core | `src/next/core` | Document 接口 |
| Next app | `src/next/app` | ProjectDocument/NextWindow/Playback+看门狗/MediaServices/Export/MCP log；5 CTest |
| Timeline | `src/next/timeline` | 行/裁剪/关键帧/拖放/浮层信号；30 测试 |
| Properties | `src/next/properties` | 属性/auto-key/求解段创建/轨迹预览；29 + 11 测试 |
| AssetPanel | `src/next/assets` | 列表/导入/标签/抽图进度；15 测试 |
| Viewport | `src/next/viewport` | 渲染/gizmo/provider；15 测试 |
| Settings | `src/next/settings` | 3 页 QSettings；14 测试 |
| MCP log | `src/next/mcp` | 日志/急停/恢复；15 测试 |

根 CMake 已包含全部新模块；`pvforge-next` 可链接。

## 4. 当前未完成 / 已知缺口

1. **崩溃恢复 UI 未接**：Store 已提供 `dirtyTextFiles/commitRecoveredText/discardDirtyText`；NextWindow 打开工程时弹窗“恢复/丢弃/提交”未做。
2. **导出最终 HTML/视频 provider**：Export2 支持 provider，ExportDialog 的高帧率/分辨率字段和 MediaServices 的 finalFrame/原片解码正在接。
3. **视频抽图队列设置**：AssetPanel 队列固定 960 宽/2GB；正在加 `setVideoExtractionOptions` 和历史视频补抽接口，主窗口接线随后。
4. **HTML 服务设置**：MediaServices 已传缓存预算/Chrome 路径；运行中改 Chrome 路径会重建服务。
5. **HTML 看门狗**：已接（MediaServices pending + PlaybackController 3s + 真实帧喂狗）；阈值跟设置。
6. **HTML Chrome 升级抽样校验**未实现（版本进 key，未抽样清资产缓存）。
7. **HTML finalFrame 线程**：导出线程调用 finalFrame 的线程模型未验证；ExportDialog 需要固定线程。
8. **视频最终 provider**：导出需原片解码，不能只用序列；正在加 export-specific VideoFrames。
9. **Asset tags MCP**：`asset.setTags` 已加；MCP 文档工具数 46 需再核对。
10. **Timeline 实体多选/框选**：第一版单选；关键帧多选已做。
11. **Timeline 私有浮层**：App 已接 `privateTimelineRequested`；主时间轴双击打 key、+ 打开浮层。
12. **旧 app 删除**：`pvforge-next` parity 后再删旧 `src/core/Project.*`、旧时间轴/场景/测试。
13. **旧测试**：大量旧测试是旧模型断言，未迁移；新目标测试已独立。
14. **崩溃恢复扫描白名单**：只认 `project.json`、`entities/*.json`、`.gitignore`；未来新增独立文本文件需扩展。
15. **内存 undo 不持久化**：重开工程只靠 git 历史冷恢复；符合设计。

## 5. 构建 / 测试

```bash
# 全量新目标
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target pvforge-next -j

# 运行（示例）
./build/src/next/app/pvforge-next --open /path/to/foo.pvproj

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

1. 接 ExportDialog：输出 fps（1x/2x/4x）、分辨率（1x/2x/4x）、音频、最终 provider。
2. 接崩溃恢复弹窗（恢复/丢弃/提交）。
3. 把 Settings 的视频抽图宽度/预算传给 AssetPanel 队列，并补抽历史视频。
4. 端到端验收工程：矢量/文字/图片/视频/HTML/音频/相机；播放、拖拽、裁剪、关键帧、求解段、MCP、导出。
5. 删除旧 app/旧模型/旧测试，`pvforge-next` 改名 `pvforge`。
