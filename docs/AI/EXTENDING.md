> **AI 文档（非用户文档）**：本文由 AI/代理生成，面向 AI 与开发者，可能滞后于代码。用户请以 `README.md`、`docs/USAGE.md`、`docs/FEATURES.md` 为准。

# PVForge 扩展指南

> 目的：新想法落地时，按清单改，不要绕过既有层。
> 原则：模型是唯一数据源；所有写操作走 `Document` → `ProjectStore` 事务；渲染、工作台缓存、MCP、Properties 必须同时覆盖新字段。

## 1. 新增 `content.kind`

以品牌墙为例，必须同时改这些层：

| 层 | 文件 | 要做的事 |
|---|---|---|
| 枚举 | `src/common/Types.h` | 加 `ContentKind` 与 JSON 名称；**追加在末尾**，不要改旧编号 |
| 模型 | `src/model/Content.h/.cpp` | 结构体、`Content` 成员、`toJson/fromJson`、`validateContent` |
| 实体校验 | `src/model/Entity.cpp` | `validateEntity` 走 `validateContent`（通常无需改，确认即可） |
| 渲染 | `src/render2/Render.cpp` | `paintContent`、`contentSize`、可动画字段（`animatedContent`） |
| 工作台缓存 | `src/workbench/MetaBuilder.cpp` | `writeContent` 序列化**所有影响画面的字段**；否则改参数不会失效 |
| Properties | `src/next/properties/PropertiesPanel.*` | 编辑区、范围校验、一次 `commitEntity` |
| 创建入口 | `src/next/app/NextWindow.cpp` | 菜单/工具栏动作，默认 start/end/row |
| MCP schema | `src/mcp2/SchemaTools.cpp`、`EntityTools.cpp` | 允许的 kind、字段、创建/更新校验 |
| 测试 | model/render2/workbench/properties/mcp2 | JSON 往返、校验、像素/包围盒、meta 变化、UI 提交、MCP |

注意：
- `render2`/`MetaBuilder` 的 switch 目前是穷举、无 `default`；新增 kind 时编译器/测试应提醒。
- 不要只加 UI 或只加模型；否则会出现“能建不能渲染/能渲染缓存不失效”。

## 2. 新增可动画轨道

| 层 | 文件 | 要做的事 |
|---|---|---|
| 轨道名 | `src/next/properties`、`src/mcp2/Support.cpp` | 统一字段名（如 `fontSize`、`vector.width`） |
| 求值 | `src/render2/Render.cpp` | `animatedContent` 用 `evaluateTrack` 覆盖静态字段 |
| 工作台 | `src/workbench/MetaBuilder.cpp` | 序列化**求值后**的值 |
| 求解约束 | `src/model/Entity.cpp` | 若属于求解驱动属性，加入段内 key 校验 |
| 测试 | render2/workbench | 打 key 后画面变化、相邻帧 meta 不同 |

## 3. 新增素材类型

| 层 | 文件 | 要做的事 |
|---|---|---|
| 枚举/检测 | `src/common/Types.h`、`src/assets/AssetTypeDetect.cpp` | `AssetType`、扩展名/MIME |
| 素材库 | `src/assets/AssetStore.cpp` | 导入、元信息、去重、缺失判断 |
| 兼容组 | `src/mcp2/AssetTools.cpp`、Properties 换素材 | 允许的旧/新类型 |
| 渲染 | `src/render2/Render.cpp` | `paintContent` 分支/provider |
| 工作台 | `MetaBuilder.cpp` | asset hash/type/sourceIn/源帧/缺失标志 |
| MCP | `SchemaTools.cpp` | `asset.import/list/get` 类型说明 |

## 4. 新增 MCP 工具

| 层 | 文件 | 要做的事 |
|---|---|---|
| 声明 | `src/mcp2/Internal.h` | handler 原型 |
| 分发 | `src/mcp2/Service.cpp` | 工具表；写工具自动进急停/只读检查 |
| 实现 | 对应 `*Tools.cpp` | 用 `Tx`，一次 `finish()` 一个 commit；批量循环检查 `stopRequested` |
| schema | `src/mcp2/SchemaTools.cpp` | 参数、错误码、guide |
| 文档 | `docs/AI/MCP_TOOLS.md` | 参数/返回/confirm/错误 |
| 测试 | `src/mcp2/tests` | 成功、回滚、confirm、只读、急停 |

工具数、端口、token 是设计规格；新增工具时同步更新文档总数。

## 5. 新增工作台缓存维度

任何影响画面的输入都必须进 `FrameMeta`：

- 实体/父链求值结果、内容字段（含轨道求值）、素材存在/源帧、HTML 变量；
- 相机/背景/输出尺寸/缓存比例/扩展比例/透明底/渲染器版本/Chrome 版本；
- 求解段版本/参数。

只改 `workbench::Options` 不够；要同时改 `MetaBuilder`，否则缓存会错误命中。

## 6. 新增 UI 面板/命令

- 只通过 `Document` 读写；不要直接摸 `ProjectStore`/`Project` 后绕过 `changed`。
- 人类写：`commitEntity/commitChanges/commitProject`（1 秒 debounce）；AI 写：MCP `Tx`。
- 多步操作先改副本、`validateProject` 通过后一次提交。
- 时间轴/属性/视口共享 `EntityRange` 等 helper，不要复制 destructive 逻辑。
- 新增快捷键要检查时间轴/文本控件焦点冲突。

## 7. 回归清单

新功能提交前至少跑：

```bash
cmake --build build --target pvforge-next
ctest --test-dir build -R '<module>\.'
```

并做一次端到端：打开工程 → 改参数 → 播放/工作台缓存 → MCP 预览 → 导出。

## 8. 当前刻意保留的扩展空间

- `content.kind` 可继续追加（漫画/线条/点阵/品牌墙已是一等公民）。
- `render2::animatedContent` 可继续加轨道字段。
- `workbench::MetaBuilder` 可继续加新元信息维度；缓存比较不做失效区间/分层。
- MCP 工具表可继续加；写工具自动获得急停/只读/confirm 框架。
- 设置页可继续加全局功能项；工程设置保持 fps/分辨率/背景/markers。
- 旧 app 删除前，`src/core` 的 `TextBlock/TextStyle/VectorShape` 与 `src/html` 的 CDP/browser 是共享件，先迁移再删。
