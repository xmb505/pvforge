# PVForge 路线图（2026-11-10）

> 依据：`docs/ENTITY_MODEL.md`、`docs/OLD_UI_FEATURES.md` 和人类逐项拍板。
> 原则：新核心不重写；旧壳能力按优先级分批迁移，先删旧 app 前保留的一等公民。

## 0. 已完成（基线）

- 新实体模型、Store（文本写盘 + 内存 undo）、时间轴、Properties、素材箱、求解段、渲染、HTML/视频缓存、MCP 46 工具、MP4 导出、崩溃恢复、设置、MCP 日志/急停。
- 端到端 MCP 验收通过（创建/关键帧/求解/marker/标签/预览/undo/redo）。
- Git 可选模式：默认关、首次引导、MCP 只读、历史面板、恢复到 commit。

## 1. 第一批：低成本高收益（旧壳删除前）

已完成：更换素材+预览、视口吸附、HTML 变量 UI、标记 CRUD、音频峰值、30 种缓动、工作台缓存、Git 可选模式/历史面板。

| 项 | 说明 | 状态 |
|---|---|---|
| 项目属性/背景色 | UI 显示名称/分辨率/fps（只读）/背景色，背景色可改 | 进行中 |
| 更换素材 + 预览 | Properties 素材页“更换素材”按钮 + 悬停大图 | 已完成 |
| 常用快捷键 | Ctrl+E 导出、F 适配、Ctrl+D 原地复制、实体 Ctrl+C/V | 进行中 |
| 视口吸附 | 像素、中心线、边缘中点、角度吸附 | 已完成 |
| HTML 变量 UI | Properties 显示 manifest `var.*` 行，可打关键帧；实体菜单“新建 HTML 层” | 变量已完成，新建入口待做 |
| 标记 CRUD | 增删改标题/颜色/备注/拖动；标记仍是全局标注，不变成实体 | 已完成 |
| 音频峰值 | 时间轴音频实体条画峰值，方便对齐 | 已完成 |

## 2. 第二批：观感与交付

| 项 | 说明 |
|---|---|
| 30 种缓动预设 | 迁旧 `EasingPicker`/求值到 `model::EasingSpec` + Properties 下拉 |
| 文字排版对话框 | 描边色、粗/斜/下划线、竖排、内边距/框宽/框高、实时预览 |
| 导出精细控制 | CRF 档位、软件/VAAPI 编码器、剩余时间/ETA、导出前预览 |
| 音频淡入淡出 | `audio2` 混音 + 模型字段 + Properties/时间轴 UI |

## 3. 第三批：PV 特殊内容（一等公民）

分阶段加入新 `content.kind`，每类都要 JSON/校验/渲染/包围盒/Gizmo/Properties/MCP：

1. 漫画框（`ComicBox`）与漫画文字
2. 径向线 / 流线（`RadialLines` / `FlowLines`）
3. 漫画点阵（`HalftoneDots`）
4. 品牌墙（`BrandWall`）

全部保留，不用 HTML 重做；旧绘制代码可复用，UI 按新壳重写。

## 4. 第四批：监看与编辑效率

| 项 | 说明 |
|---|---|
| 相机监视器 | 可拖出浮窗，实时显示当前相机；**不做** HTTP/RTP 串流 |
| 临摹参考 | 参考视频透明度叠加 |
| 实体多选/框选 | 批量移动/删除/复制；成组仍用空实体 |

## 5. 第五批：旧壳退休

- 删除旧 `src/app`、`src/ui`、`src/core`（先移出 `TextBlock/TextStyle/VectorShape`）、`src/render`、`src/media`、`src/audio`、`src/mcp`、旧 html 壳。
- `pvforge-next` 改名 `pvforge`；更新文档/命令。
- 保留 `src/html` 的 CDP/browser 实现给 `html2`。

## 6. 暂不做

- 旧场景/注释/检查点系统。
- 旧语言包（49 个 key，半成品）；以后用 Qt `.ts` 重做。
- 旧曲线图编辑器（旧壳已隐藏）。
- 相机监视器 HTTP/RTP 串流。
