# PVForge MCP 工具文档

> 面向通过 MCP 操作 PVForge 的 AI 客户端。
>
> 本文定义 **新实体模型**（见 [ENTITY_MODEL.md](ENTITY_MODEL.md)）的 MCP 契约：工具名、参数、返回、错误码和完整工作流。
> 实现位于 `src/mcp2/`；以本文、`tools/list` 和 `schema.get` 为准。
> 旧模型的 `timeline.get`、`assets.list`、`easing.set`、`entities.retire`、`scene.*`、`batch.run` 不属于新接口，不要混用。

---

## 0. 文档约定

### 0.1 传输与结果信封

- 传输：MCP Streamable HTTP，`POST /mcp`，JSON-RPC 2.0；只接受 loopback。
- 工具结果统一是结构化 JSON：
  - 成功：`{ "ok": true, "data": {...}, "page": {...}?, "commit": {...}? }`
  - 失败：`{ "ok": false, "error": { "code": "...", "message": "...", "details": {...}? } }`
- MCP 层会把同一对象放进 `structuredContent`，并在 `content[0].text` 放紧凑 JSON；失败时 `isError = true`。
- `page` 只出现在分页读取工具上。
- `commit` 只出现在写工具上；一次成功调用对应一个 git commit。
- 下文写工具的返回简写中，`commit` 一律指**顶层字段**，不属于 `data`。
- 所有帧号都是**整数帧**，不接受小数。
- 所有 UUID 都是小写、带连字符的 UUID v4；`null` 表示根实体或“无”。
- 所有长度单位默认是**像素**；旋转单位是**度**；时间单位是**帧**，除非字段名明确写 `Seconds`。
- 所有数值必须是有限数，绝对值不超过 `1e9`；帧号不超过 `1e6`。

### 0.2 错误码

| code | 含义 | AI 是否应重试 |
|---|---|---|
| `invalid_argument` | 缺字段、类型错、枚举未知、帧号越界、轨道不存在、素材不存在 | 修正参数后可重试一次 |
| `permission_denied` | 写权限未开启、工具被人类禁用、操作超出授权范围 | 不自动重试，报告人类 |
| `confirm_required` | 危险操作缺少 `confirm: true` | 先向人类确认，再带 `confirm` 重试 |
| `conflict` | 实体锁定、求解段重叠、关键帧冲突、父子范围冲突、git 冲突 | 不自动重试；先读状态再决定 |
| `limit_exceeded` | 批量超过 200、实体超过 5000、分页超过 500、采样超过 10000、预览限流 | 降低批量或等待后重试 |
| `stopped_by_human` | 人类按了急停 | **绝不重试** |
| `not_available` | 功能未实现、缺 git/ffmpeg/Chrome 等依赖 | 不重试，报告人类 |
| `render_failed` | 预览渲染失败 | 检查素材和摄像机后可重试一次 |
| `render_timeout` | 预览超时 | 降低分辨率或简化画面后重试一次 |
| `internal_error` | 未预期错误 | 不重试，报告人类 |

`details` 是可选对象，常见键：

| 键 | 用途 |
|---|---|
| `allowedRange: {min,max}` | 帧号/数值允许范围 |
| `allowedTracks: [...]` | 该实体允许的轨道名 |
| `missing: [...]` | 批量里不存在或重复的 UUID |
| `index: n` | 批量中第几个元素失败（从 0 开始） |
| `lockedBy: "uuid"` | 哪个实体（自己或祖先）锁定了写入 |
| `conflicts: [{solverId,startFrame,endFrame}]` | 与哪些求解段冲突 |
| `requires: "confirm"`、`affected: {...}` | 需要确认及影响范围 |
| `dirtyPaths: [...]` | git 工作区未提交的文件 |

### 0.3 帧、单位与坐标

| 概念 | 规则 |
|---|---|
| 实体 `start` / `end` | **全局帧号**，整数，半开区间 `[start, end)`，`end > start` |
| `duration` | `end - start`，至少 1 |
| 关键帧 `frame` | **相对实体 start**，整数，只允许 `0 .. duration-1` |
| 求解段 `startFrame` / `endFrame` | **相对实体 start**，整数；段是闭区间 `[startFrame, endFrame]` |
| `endFrame = -1` | 求解到 clip 最后一帧，即 `duration-1` |
| 标记 `markers.frame` | **全局帧号** |
| 预览 `frame`、`view.state` 帧号 | **全局帧号** |
| 坐标 | 2D 像素；原点在画面中心，x 向右，y 向下 |
| 旋转 | 度；y 向下，所以正值在屏幕上顺时针 |
| 缩放 | 倍数，`1 = 100%`；负值表示翻转 |
| 透明度 / 音量 | 透明度 `0..1`；音量 `>= 0`，默认 1 |

关键帧写到 `duration` 是**越界**，不是“结尾标记”。求解段的结束帧才是标记，且不存值。

### 0.4 提交、撤销与原子性

- **写文本的工具调用 = 一个逻辑操作 = 一个 commit。**素材文件不进 git；纯 `asset.import` 没有文本 diff 时可能**没有 commit**。
- 批量工具（`entity.createTree`、`entity.createMany`、`entity.updateMany`、`entity.deleteMany`、`keyframes.setMany`）在一次调用里只产生**一个** commit（如果有文本变更）。
- 批量工具**全成功或全回滚**：任何一个元素校验失败，整次调用不写文件、不产生 commit，返回第一个错误（带 `details.index`）。
- 失败和回滚不会留下部分实体、部分关键帧或半个素材。
- 只读工具不产生 commit：`*.get`、`*.list`、`solver.evaluate`、`preview.render_frame`、`view.state`、`schema.get`、`guide.get`、`history.log`。
- `history.undo` / `history.redo` 是**内存快照撤销/重做**，不产生 git commit；git 历史是冷恢复。
- commit 消息自动生成，例如：
  - `pvf: create entity "标题"`
  - `pvf: create tree "标题" (4 entities)`
  - `pvf: set keyframes "标题" x`
  - `pvf: create solver "小球" gravity`
  - `pvf: delete 3 entities`
- 写文本成功可能返回 `commit: {hash, shortHash, message}`；纯素材导入时 `commit` 为空/省略。


### 0.5 权限、锁定与 confirm

- MCP 默认可以读全工程，默认也可写；删除父级、批量删除、删除素材等危险操作要求 `confirm: true`。
- 人类急停后，所有写工具返回 `stopped_by_human`，读工具仍可用；急停后不要自动重试。
- **锁定实体写入会失败。** `locked` 会沿父链生效：实体自己锁定，或任一祖先锁定，都算锁定。
  - 读取、预览、复制不受影响。
  - 写入、移动、裁剪、换父级、删除、加子实体都会返回 `conflict`，`details.lockedBy` 指向锁定源。
  - MCP **不能解锁**；解锁只能由人类在编辑器里操作。不要用 `entity.update {locked:false}` 绕过，锁定实体的任何 patch 都会先被拒绝。
  - `hidden` 只影响编辑器显示，不阻止写入。
- 危险操作必须显式传 `confirm: true`（布尔值，不接受字符串）：

| 工具 | 何时需要 confirm |
|---|---|
| `entity.delete` | 总是 |
| `entity.deleteMany` | 总是 |
| `asset.delete` | 总是 |
| `entity.trim` | 本次裁剪会删除至少一个子实体时 |
| `project.update` | 工程非空且修改 `fps` 时 |

缺少 `confirm` 返回 `confirm_required`，`details.affected` 会列出将被删除的实体或受影响的帧范围。

### 0.6 通用对象

#### 实体完整对象

```json
{
  "uuid": "0190f2c4-7a1e-7c3b-9d2a-1f4e6b8c0d12",
  "name": "标题",
  "parentUuid": null,
  "start": 0,
  "end": 90,
  "duration": 90,
  "row": 0,
  "hidden": false,
  "locked": false,
  "inheritVisibility": true,
  "visible": true,
  "physicsInteract": false,
  "transform": {
    "x": 0, "y": 0, "rotation": 0,
    "scaleX": 1, "scaleY": 1,
    "pivotX": 0, "pivotY": 0,
    "opacity": 1
  },
  "content": { "kind": "empty" },
  "keyframes": {
    "x": [
      { "frame": 0, "value": 0, "easing": "linear" }
    ]
  },
  "solvers": []
}
```

实体简要对象只含：`uuid`、`name`、`parentUuid`、`start`、`end`、`duration`、`row`、`hidden`、`locked`、`contentKind`。

#### `transform`

| 字段 | 类型 | 默认 | 单位 | 说明 |
|---|---|---|---|---|
| `x` | number | 0 | px | 局部坐标；相对父实体轴心 |
| `y` | number | 0 | px | 向下为正 |
| `rotation` | number | 0 | deg | 正值顺时针 |
| `scaleX` | number | 1 | 倍数 | 负值水平翻转 |
| `scaleY` | number | 1 | 倍数 | 负值垂直翻转 |
| `pivotX` | number | 0 | px | 轴心，局部坐标 |
| `pivotY` | number | 0 | px | 轴心，局部坐标 |
| `opacity` | number | 1 | 0..1 | 沿父链相乘 |

#### `content`

`content.kind` 五选一：`empty`、`text`、`vector`、`camera`、`asset`。

`text`：

| 字段 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `text` | string | `""` | 文本内容，最长 4096 字符 |
| `fontFamily` | string | `""` | 空字符串表示编辑器默认字体 |
| `pixelSize` | integer | 96 | 字号，px |
| `color` | string | `#ffffff` | 填充色 |
| `strokeColor` | string | `#000000` | 描边色 |
| `strokeWidth` | number | 0 | 描边宽度，px |
| `align` | integer | 132 | Qt 对齐位掩码；132 = 水平居中 + 垂直居中 |
| `lineHeight` | number | 1.0 | 行高倍数 |
| `letterSpacing` | number | 0 | 字距，px |
| `padding` | number | 6 | 内边距，px |
| `boxWidth` | number | 0 | 文本框宽，0 = 自动 |
| `boxHeight` | number | 0 | 文本框高，0 = 自动 |
| `bold` | boolean | false | 粗体 |
| `italic` | boolean | false | 斜体 |
| `underline` | boolean | false | 下划线 |
| `vertical` | boolean | false | 竖排 |
| `verticalRtl` | boolean | true | 竖排从右到左 |

`vector`：

| 字段 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `shape` | integer | 0 | 形状枚举；具体值见 `schema.get("entity")`（尚未定稿） |
| `width` | number | 400 | px |
| `height` | number | 300 | px |
| `cornerRadius` | number | 40 | 圆角，px |
| `sides` | integer | 5 | 多边形边数 |
| `innerRatio` | number | 0.45 | 星形内外半径比 |
| `fillColor` | string | `#ffffff` | 填充色 |
| `strokeColor` | string | `#000000` | 描边色 |
| `strokeWidth` | number | 0 | 描边宽度，px |

`camera`：`{}`，没有额外字段；取景由 `transform` 的 `x/y/rotation/scaleX/scaleY` 描述。

`asset`：

| 字段 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `assetHash` | string | 必填 | 64 位小写 sha256 |
| `assetType` | string | 必填 | `image` / `video` / `audio` / `html`，缓存值 |
| `sourceIn` | integer | 0 | 视频/音频：实体 start 对应素材内第几帧 |
| `volume` | number | 1.0 | 仅音频；>= 0 |

`empty`：`{ "kind": "empty" }`。

内容类型切换规则：

| 目标 kind | 允许来源 | 使用工具 |
|---|---|---|
| `empty` | 任意 kind | `entity.update` 或 `asset.unassign` |
| `text` / `vector` | `empty`、`text`、`vector` | `entity.update` |
| `camera` | `empty`、`camera` | `entity.update` |
| `asset` | `empty`、同组 `asset` | `asset.assign` / `asset.unassign` |

素材兼容组：视觉组 `image`/`video`/`html` 可互换；生成组 `text`/`vector` 可互换；`audio` 只能换 `audio`；`camera` 不能换素材。

#### 关键帧

```json
{ "frame": 12, "value": 1, "easing": "easeoutcubic", "note": "入场结束" }
```

| 字段 | 类型 | 必填 | 默认 | 说明 |
|---|---|---|---|---|
| `frame` | integer | 是 | — | 相对实体 start，`0 .. duration-1` |
| `value` | number | 是 | — | 轨道值；布尔轨道用 0/1 |
| `easing` | string 或 object | 否 | `"linear"` | 见下 |
| `note` | string | 否 | `""` | 备注，最长 4096 字符，不参与渲染 |

`easing` 字符串预设（小写）：`linear`、`instant`、`bezier`、`easeinquad`、`easeoutquad`、`easeinoutquad`、`easeincubic`、`easeoutcubic`、`easeinoutcubic`、`easeinquart`、`easeoutquart`、`easeinoutquart`、`easeinquint`、`easeoutquint`、`easeinoutquint`、`easeinsine`、`easeoutsine`、`easeinoutsine`、`easeinexpo`、`easeoutexpo`、`easeinoutexpo`、`easeincirc`、`easeoutcirc`、`easeinoutcirc`、`easeinelastic`、`easeoutelastic`、`easeinoutelastic`、`easeinback`、`easeoutback`、`easeinoutback`、`easeinbounce`、`easeoutbounce`、`easeinoutbounce`。

自定义贝塞尔：

```json
{ "type": "bezier", "inX": 0.42, "inY": 0, "outX": 0.58, "outY": 1 }
```

P0 固定 `(0,0)`，P3 固定 `(1,1)`；`inX/outX` 会被夹到 `0..1`，`inY/outY` 允许过冲（回弹类曲线）。

缓动属于**到达的关键帧**：给第 12 帧设 `easeoutcubic`，影响的是上一关键帧到第 12 帧这一段。0 帧没有进入段，设置缓动没有视觉效果。

#### 轨道清单

所有实体通用：`x`、`y`、`rotation`、`scaleX`、`scaleY`、`opacity`、`pivotX`、`pivotY`、`visible`、`physicsInteract`。

按内容追加：

| 内容 | 额外轨道 |
|---|---|
| `text` | `fontSize`、`lineHeight`、`letterSpacing`、`strokeWidth`、`padding`、`boxWidth`、`boxHeight` |
| `vector` | `width`、`height`、`cornerRadius`、`strokeWidth` |
| `audio` | `volume` |
| `html` | `var.<name>`（manifest 声明的数字变量） |
| `camera` | 只有 `x`、`y`、`rotation`、`scaleX`、`scaleY`；没有 `opacity` |
| `image` / `video` / `empty` | 只有通用轨道 |

轨道值约定：

| 轨道 | 单位/范围 | 说明 |
|---|---|---|
| `x`、`y`、`pivotX`、`pivotY` | px | 局部坐标 |
| `rotation` | deg | 正值顺时针 |
| `scaleX`、`scaleY` | 倍数 | 可为负 |
| `opacity` | 0..1 | — |
| `visible`、`physicsInteract` | 0 / 1 | step 插值；`easing` 无效 |
| `fontSize` | px，> 0 | 对应文字内容的 `pixelSize` |
| `lineHeight` | 倍数，> 0 | — |
| `letterSpacing`、`padding` | px | — |
| `boxWidth`、`boxHeight` | px，0 = 自动 | — |
| `width`、`height`、`cornerRadius` | px，>= 0 | — |
| `strokeWidth` | px，>= 0 | — |
| `volume` | >= 0 | 仅音频 |
| `var.<name>` | 由 manifest 决定 | 仅 HTML |

第一版**不可关键帧**：颜色、字体、对齐、粗体/斜体/下划线、竖排、矢量 `sides`/`innerRatio`。

#### 求解段

```json
{
  "id": "0190f2c4-8b2e-7d4c-a1b3-2c5d7e9f0a13",
  "effect": "gravity",
  "startFrame": 0,
  "endFrame": 119,
  "version": 1,
  "initialVelocity": { "x": 540, "y": -240 },
  "params": { "strength": 980, "elasticity": 0.45, "friction": 0.2, "stopThreshold": 3 }
}
```

- `id` 由服务端生成。
- `effect`：`gravity`、`orbit`、`spin`、`blackhole`、`spiral`。
- `startFrame` / `endFrame`：相对实体 start；`endFrame = -1` 表示到 clip 最后一帧。
- `version`：解算器版本，只读；预览和导出使用同一版本。
- `initialVelocity`：px/s，求解空间（画布/父级空间）；只有 `gravity` / `blackhole` 使用。
- `params`：按效果取字段，见第 5 节。

#### 素材对象

```json
{
  "hash": "9f2c...64位...",
  "type": "video",
  "name": "opening.mp4",
  "mime": "video/mp4",
  "width": 1920,
  "height": 1080,
  "durationFrames": 240,
  "sampleRate": 0,
  "channels": 0,
  "entry": null,
  "variables": null,
  "tags": ["片头", "season-1"],
  "usedBy": ["uuid1"],
  "missing": false,
  "path": "/project/assets/9f2c.../content.mp4"
}
```

- `type`：`image`、`video`、`audio`、`html`。
- `durationFrames`：视频/音频的帧数；图片/HTML 为 0。
- `sampleRate` / `channels`：仅音频。
- `entry` / `variables`：仅 HTML。
- `tags`：素材标签，字符串数组；最多 32 个，单个 1..64 字符。
- `usedBy`：引用该素材的实体 UUID。
- `missing`：素材文件不存在时为 `true`。

#### 分页与 commit

```json
{ "page": { "offset": 0, "limit": 100, "total": 12, "hasMore": false, "nextOffset": null } }
```

```json
{
  "commit": {
    "hash": "a1b2c3d4e5f60718293a4b5c6d7e8f9012345678",
    "shortHash": "a1b2c3d",
    "message": "pvf: create entity \"标题\""
  }
}
```

分页默认 `limit = 100`，最大 `500`。

---

## 1. 工具总览

| 家族 | 工具 | 数量 | 写 | commit | confirm |
|---|---|---|---|---|---|
| `project` | `project.get`、`project.summary`、`project.update` | 3 | 部分 | `update` 有 | fps/分辨率锁定，传了拒绝 |
| `entity` | `create`、`createTree`、`createMany`、`get`、`list`、`update`、`updateMany`、`move`、`trim`、`reparent`、`duplicate`、`delete`、`deleteMany` | 13 | 部分 | 写工具各 1 个 | `delete`、`deleteMany`；`trim` 级联删子实体时 |
| `keyframes` | `get`、`set`、`setMany`、`remove`、`move`、`clear`、`setEasing` | 7 | 部分 | 写工具各 1 个 | 否 |
| `solver` | `create`、`update`、`delete`、`list`、`evaluate` | 5 | 部分 | 写工具各 1 个 | 否 |
| `asset` | `import`、`list`、`get`、`assign`、`unassign`、`rename`、`setTags`、`delete` | 8 | 部分 | 写工具各 1 个 | `delete` |
| `markers` | `list`、`set`、`remove` | 3 | 部分 | 写工具各 1 个 | 否 |
| `preview` | `render_frame` | 1 | 否 | 无 | 否 |
| `view` | `state` | 1 | 否 | 无 | 否 |
| `schema` | `get` | 1 | 否 | 无 | 否 |
| `guide` | `get` | 1 | 否 | 无 | 否 |
| `history` | `undo`、`redo`、`log` | 3 | 部分 | `undo`/`redo` 有 | 否 |

合计 **46 个工具**。表中 `entity` / `keyframes` / `solver` / `asset` / `markers` / `history` 行省略了家族前缀，完整名为 `entity.create`、`keyframes.set` 等。

写工具的通用前置检查：

1. 工程已打开且可写；否则 `permission_denied` 或 `not_available`。
2. 人类未急停；否则 `stopped_by_human`。
3. 目标实体存在；否则 `invalid_argument`。
4. 目标实体及其祖先未锁定；否则 `conflict`。
5. 危险操作已带 `confirm: true`；否则 `confirm_required`。
6. 批量/树/轨道写入通过全部校验；否则整次回滚。

---

## 2. project

### 2.1 `project.get` — 只读

读取工程设置、路径、计数和 git 状态。

**参数**：无。

**返回 `data`**：

| 字段 | 类型 | 说明 |
|---|---|---|
| `settings` | object | `{name,width,height,fps,background}` |
| `paths` | object | `{root,projectJson,entitiesDir,assetsDir}`，服务器本地路径 |
| `counts` | object | `{entities,assets,markers}` |
| `git` | object | `{available,branch,head,clean}` |
| `write` | object | `{enabled,stopped}`，当前 AI 写权限状态 |

**错误**：`not_available`（工程未打开）、`internal_error`。

**示例**：

```json
{}
```

```json
{
  "ok": true,
  "data": {
    "settings": { "name": "未命名项目", "width": 1920, "height": 1080, "fps": 30, "background": "#101014" },
    "paths": {
      "root": "/home/user/demo.pvproj",
      "projectJson": "/home/user/demo.pvproj/project.json",
      "entitiesDir": "/home/user/demo.pvproj/entities",
      "assetsDir": "/home/user/demo.pvproj/assets"
    },
    "counts": { "entities": 12, "assets": 4, "markers": 3 },
    "git": { "available": true, "branch": "main", "head": "a1b2c3d", "clean": true },
    "write": { "enabled": true, "stopped": false }
  }
}
```

### 2.2 `project.summary` — 只读

轻量概况；AI 开始任务时先读这个。

**参数**：无。

**返回 `data`**：

| 字段 | 类型 | 说明 |
|---|---|---|
| `name` | string | 工程名 |
| `width` / `height` | integer | 画布尺寸，px |
| `fps` | integer | 帧率 |
| `durationFrames` | integer | 当前内容长度 = 所有实体 `end` 的最大值；空工程为 0 |
| `durationSeconds` | number | `durationFrames / fps` |
| `background` | string | 背景色 |
| `entityCount` | integer | 实体数 |
| `assetCount` | integer | 素材数 |
| `markerCount` | integer | 标记数 |
| `activeCameraUuid` | string 或 null | 当前活动摄像机 |
| `writeEnabled` | boolean | 是否允许 AI 写 |

**错误**：`not_available`。

**示例**：

```json
{}
```

```json
{
  "ok": true,
  "data": {
    "name": "90 帧标题",
    "width": 1920,
    "height": 1080,
    "fps": 30,
    "durationFrames": 90,
    "durationSeconds": 3.0,
    "background": "#101014",
    "entityCount": 3,
    "assetCount": 0,
    "markerCount": 0,
    "activeCameraUuid": null,
    "writeEnabled": true
  }
}
```

### 2.3 `project.update` — 写

修改工程设置。

**参数**：

| 字段 | 类型 | 必填 | 默认 | 说明 |
|---|---|---|---|---|
| `name` | string | 否 | — | 工程名，1..256 字符 |
| `width` | integer | 否 | — | **创建后锁定，传了会被拒绝** |
| `height` | integer | 否 | — | **创建后锁定，传了会被拒绝** |
| `fps` | integer | 否 | — | **创建后锁定，传了会被拒绝** |
| `background` | string | 否 | — | CSS hex 颜色 |

**返回 `data`**：`{settings, changed:[...]}`。

**错误**：`invalid_argument`、`conflict`（fps/分辨率锁定、git 提交失败）、`permission_denied`。

**示例**：

```json
{ "background": "#0b1020", "name": "片头 90 帧" }
```

**注意**：fps 和分辨率在创建工程时锁定，之后不能改；需要新参数请新建工程。渲染页可以另设输出 fps（2x/4x）和分辨率（1x/2x/4x），但不改工程数据。

---

## 3. entity

### 3.1 `entity.create` — 写

创建一个实体。`start` 必须显式给出（新模型规则）。

**参数**：

| 字段 | 类型 | 必填 | 默认 | 说明 |
|---|---|---|---|---|
| `name` | string | 否 | 按内容取默认 | 1..256 字符 |
| `parentUuid` | string 或 null | 否 | null | 父实体；`null` 为根 |
| `start` | integer | **是** | — | 全局帧号，>= 0 |
| `end` | integer | 否 | 见下 | 全局帧号；必须 > `start` |
| `row` | integer | 否 | 自动 | >= 0；自动 = 同级范围内最高占用 row + 1 |
| `content` | object | 否 | `{"kind":"empty"}` | 见 0.6 |
| `transform` | object | 否 | 默认变换 | 部分字段可省略 |
| `hidden` | boolean | 否 | false | 编辑器隐藏 |
| `locked` | boolean | 否 | false | 创建后立即锁定 |
| `inheritVisibility` | boolean | 否 | true | 是否继承父级可见性 |
| `visible` | boolean | 否 | true | 渲染可见 |
| `physicsInteract` | boolean | 否 | false | 是否参与碰撞 |
| `keyframes` | object | 否 | `{}` | `{track: [key,...]}` |
| `solvers` | array | 否 | `[]` | 求解段，字段同 `solver.create` 去掉 `entityUuid` |

**`end` 默认值**：

| 内容 | 默认 end |
|---|---|
| `video` / `audio` | `start + (asset.durationFrames - sourceIn)` |
| `image` / `html` / `text` / `vector` / `empty` / `camera` | `start + fps`，即 1 秒 |

**规则**：

- `parentUuid` 必须存在且未锁定；子实体 `[start,end)` 必须落在父实体范围内。
- `video`/`audio` 必须满足 `sourceIn + (end-start) <= asset.durationFrames`。
- `row` 自动分配只避免默认重叠；显式给 `row` 允许重叠。
- `keyframes` 的帧号相对**新实体**的 `start`，必须在 `0..duration-1`。
- `solvers` 的帧号同样相对**新实体**的 `start`；开始帧必须已有被驱动轨道的关键帧（可在同一个 `keyframes` 里给；服务端先应用 `keyframes`，再校验 `solvers`）。
- 名称默认：素材名 / `文字` / `形状` / `摄像机` / `实体`。
- 一次调用一个 commit。

**返回 `data`**：`{uuid, entity, commit}`；`entity` 是简要对象。

**错误**：`invalid_argument`（缺 `start`、范围非法、轨道未知、素材不存在、`end` 超出素材）、`permission_denied`、`conflict`（父级范围、父级锁定、求解段冲突）、`limit_exceeded`（工程实体超过 5000）。

**示例**：

```json
{
  "name": "标题文字",
  "parentUuid": null,
  "start": 0,
  "end": 90,
  "row": 1,
  "content": {
    "kind": "text",
    "text": {
      "text": "PVFORGE",
      "fontFamily": "Noto Sans SC",
      "pixelSize": 180,
      "color": "#ffffff",
      "bold": true,
      "align": 132
    }
  },
  "transform": { "x": 0, "y": -10, "opacity": 1 }
}
```

```json
{
  "ok": true,
  "data": {
    "uuid": "0190f2c4-7a1e-7c3b-9d2a-1f4e6b8c0d12",
    "entity": {
      "uuid": "0190f2c4-7a1e-7c3b-9d2a-1f4e6b8c0d12",
      "name": "标题文字",
      "parentUuid": null,
      "start": 0,
      "end": 90,
      "duration": 90,
      "row": 1,
      "hidden": false,
      "locked": false,
      "contentKind": "text"
    }
  },
  "commit": { "hash": "a1b2c3d4e5f60718293a4b5c6d7e8f9012345678", "shortHash": "a1b2c3d", "message": "pvf: create entity \"标题文字\"" }
}
```

### 3.2 `entity.createTree` — 写

一次创建父实体、子实体、行号、关键帧和求解段。**整棵树一个 commit。**

**参数**：

| 字段 | 类型 | 必填 | 默认 | 说明 |
|---|---|---|---|---|
| `parentUuid` | string 或 null | 否 | null | 整棵树挂到哪个父实体下 |
| `root` | object | **是** | — | 根节点，字段见下 |

**节点对象**：

| 字段 | 类型 | 必填 | 默认 | 说明 |
|---|---|---|---|---|
| `name` | string | 否 | 按内容 | 1..256 字符 |
| `content` | object | 否 | empty | 见 0.6 |
| `start` | integer | 根**是**；子否 | 子 = 父 `start` | 全局帧号 |
| `end` | integer | 否 | 根按内容；子 = 父 `end` | 全局帧号 |
| `row` | integer | 否 | 自动 | 同级范围内自动分配 |
| `transform` | object | 否 | 默认 | 部分字段 |
| `hidden` / `locked` / `inheritVisibility` / `visible` / `physicsInteract` | boolean | 否 | 同 `entity.create` | — |
| `keyframes` | object | 否 | `{}` | 相对本节点 `start` |
| `solvers` | array | 否 | `[]` | 相对本节点 `start` |
| `children` | array | 否 | `[]` | 子节点，递归同结构 |

**规则**：

- 最大深度 64，单棵树最多 200 个节点。
- 子节点范围必须落在父节点 `[start,end)` 内；越界是 `conflict`。
- 子节点省略 `start` 时取父 `start`，省略 `end` 时取父 `end`。
- `row` 在每个父级内独立计算；row 0 最底，row 越大越靠前。
- 关键帧、求解段的帧号都相对**各自节点**的 `start`；同一个节点里先应用 `keyframes`，再校验 `solvers`。
- 全部节点校验通过才落盘；任何失败整棵树回滚。

**返回**：

```json
{
  "ok": true,
  "data": {
    "rootUuid": "...",
    "uuids": ["...", "..."],
    "mapping": [
      { "path": "0", "uuid": "..." },
      { "path": "0.0", "uuid": "..." },
      { "path": "0.1", "uuid": "..." }
    ],
    "entityCount": 3
  },
  "commit": { "hash": "...", "shortHash": "...", "message": "pvf: create tree \"卡片\" (3 entities)" }
}
```

`path` 是从根开始的子节点下标路径；根是 `"0"`，第一个子节点是 `"0.0"`。

**错误**：`invalid_argument`、`conflict`（范围、父级锁定）、`limit_exceeded`（节点 > 200 或工程 > 5000）、`permission_denied`。

**示例**：

```json
{
  "parentUuid": null,
  "root": {
    "name": "卡片",
    "start": 0,
    "end": 60,
    "row": 0,
    "content": { "kind": "empty" },
    "children": [
      {
        "name": "背景",
        "start": 0,
        "end": 60,
        "row": 0,
        "content": {
          "kind": "vector",
          "vector": { "width": 800, "height": 200, "cornerRadius": 24, "fillColor": "#1e293b" }
        }
      },
      {
        "name": "标题",
        "start": 0,
        "end": 60,
        "row": 1,
        "content": {
          "kind": "text",
          "text": { "text": "Hello", "pixelSize": 64, "align": 132 }
        },
        "transform": { "y": -10 }
      },
      {
        "name": "进度条",
        "start": 0,
        "end": 60,
        "row": 2,
        "content": {
          "kind": "vector",
          "vector": { "width": 600, "height": 12, "cornerRadius": 6, "fillColor": "#ffbe3c" }
        },
        "keyframes": {
          "scaleX": [
            { "frame": 0, "value": 0, "easing": "easeoutquart" },
            { "frame": 59, "value": 1, "easing": "linear" }
          ]
        }
      }
    ]
  }
}
```

### 3.3 `entity.createMany` — 写

一次创建多个**互不嵌套**的实体。**整批一个 commit。**

**参数**：

| 字段 | 类型 | 必填 | 默认 | 说明 |
|---|---|---|---|---|
| `entities` | array | **是** | — | 1..200 个实体对象，字段同 `entity.create` |

**规则**：

- 数组里的 `parentUuid` 必须指向**调用前已存在**的实体；不能在同一个数组里引用刚创建的对象。需要新层级时用 `entity.createTree`。
- 每个元素独立校验；任何一个失败整批回滚。
- 同一个数组里允许出现重名实体；`name` 不要求唯一。

**返回 `data`**：`{uuids, created:[{uuid,name}], commit}`。

**错误**：同 `entity.create`；批量失败时 `details.index` 指向出错元素。

**示例**：

```json
{
  "entities": [
    {
      "name": "左字幕",
      "start": 0,
      "end": 60,
      "row": 0,
      "content": { "kind": "text", "text": { "text": "A", "pixelSize": 48 } },
      "transform": { "x": -300, "y": 200 }
    },
    {
      "name": "右字幕",
      "start": 0,
      "end": 60,
      "row": 1,
      "content": { "kind": "text", "text": { "text": "B", "pixelSize": 48 } },
      "transform": { "x": 300, "y": 200 }
    }
  ]
}
```

### 3.4 `entity.get` — 只读

读取单个实体完整状态，并返回指定帧的求值结果。

**参数**：

| 字段 | 类型 | 必填 | 默认 | 说明 |
|---|---|---|---|---|
| `uuid` | string | **是** | — | 实体 UUID |
| `frame` | integer | 否 | 当前播放头 | 全局帧号，用于 `evaluated` |
| `includeChildren` | boolean | 否 | false | 是否返回直接子实体简要列表 |

**返回 `data`**：实体完整对象 + `evaluated` + `children?` + `asset?`。

`evaluated` 至少包含：`x`、`y`、`rotation`、`scaleX`、`scaleY`、`opacity`、`visible`；音频实体另含 `volume`。

**错误**：`invalid_argument`（UUID 不存在、frame 非法）。

**示例**：

```json
{ "uuid": "0190f2c4-7a1e-7c3b-9d2a-1f4e6b8c0d12", "frame": 45, "includeChildren": true }
```

```json
{
  "ok": true,
  "data": {
    "uuid": "0190f2c4-7a1e-7c3b-9d2a-1f4e6b8c0d12",
    "name": "标题文字",
    "parentUuid": null,
    "start": 0,
    "end": 90,
    "duration": 90,
    "row": 1,
    "hidden": false,
    "locked": false,
    "inheritVisibility": true,
    "visible": true,
    "physicsInteract": false,
    "transform": { "x": 0, "y": -10, "rotation": 0, "scaleX": 1, "scaleY": 1, "pivotX": 0, "pivotY": 0, "opacity": 1 },
    "content": { "kind": "text", "text": { "text": "PVFORGE", "pixelSize": 180 } },
    "keyframes": { "opacity": [{ "frame": 0, "value": 0, "easing": "linear" }] },
    "solvers": [],
    "evaluated": { "x": 0, "y": -10, "rotation": 0, "scaleX": 1, "scaleY": 1, "opacity": 0.5, "visible": true },
    "children": []
  }
}
```

### 3.5 `entity.list` — 只读

按父级/根层分页列出实体简要对象。

**参数**：

| 字段 | 类型 | 必填 | 默认 | 说明 |
|---|---|---|---|---|
| `parentUuid` | string 或 null | 否 | 省略 = 全部 | 给字符串只看该父级的直接子级；给 `null` 只看根实体 |
| `offset` | integer | 否 | 0 | >= 0 |
| `limit` | integer | 否 | 100 | 1..500 |
| `includeKeyframes` | boolean | 否 | false | 是否带 `keyframes` |
| `includeSolvers` | boolean | 否 | false | 是否带 `solvers` |

**返回 `data`**：`{entities:[实体简要对象], page}`。

排序：`row` 升序 → `start` 升序 → `uuid` 升序。

**错误**：`invalid_argument`（`parentUuid` 不存在、分页参数非法）、`limit_exceeded`（`limit > 500`）。

**示例**：

```json
{ "parentUuid": null, "limit": 50, "includeKeyframes": true }
```

```json
{
  "ok": true,
  "data": {
    "entities": [
      { "uuid": "...", "name": "标题组", "parentUuid": null, "start": 0, "end": 90, "duration": 90, "row": 0, "hidden": false, "locked": false, "contentKind": "empty", "keyframes": {}, "solvers": [] }
    ]
  },
  "page": { "offset": 0, "limit": 50, "total": 1, "hasMore": false, "nextOffset": null }
}
```

### 3.6 `entity.update` — 写

局部修改实体。**不能改时间范围、父级、关键帧、求解段、素材引用**；这些用专用工具。

**参数**：

| 字段 | 类型 | 必填 | 默认 | 说明 |
|---|---|---|---|---|
| `uuid` | string | **是** | — | 实体 UUID |
| `patch` | object | **是** | — | 要修改的字段，见下 |

**`patch` 允许的字段**：

| 字段 | 类型 | 说明 |
|---|---|---|
| `name` | string | 1..256 字符 |
| `row` | integer | >= 0；只改本实体在兄弟中的顺序 |
| `hidden` | boolean | 编辑器隐藏 |
| `locked` | boolean | 只能设为 `true`；已锁定实体不能通过 patch 解锁 |
| `inheritVisibility` | boolean | — |
| `visible` | boolean | 静态可见值 |
| `physicsInteract` | boolean | 静态碰撞开关 |
| `transform` | object | 部分合并；只改给出的轴 |
| `content` | object | 部分合并；`kind` 相同则改字段，`kind` 变化按 0.6 的兼容表 |
| `content.asset.sourceIn` | integer | 视频/音频源偏移；必须满足 `sourceIn + duration <= asset.durationFrames` |
| `content.asset.volume` | number | 音频音量，>= 0 |

**不允许**：`uuid`、`parentUuid`、`start`、`end`、`keyframes`、`solvers`、`content.asset.assetHash`、`content.asset.assetType`。

**返回 `data`**：`{uuid, changed:[...], entity, commit}`。

**错误**：`invalid_argument`（未知字段、范围、类型不兼容）、`conflict`（锁定、素材长度、父级锁定）、`permission_denied`。

**示例**：

```json
{
  "uuid": "0190f2c4-7a1e-7c3b-9d2a-1f4e6b8c0d12",
  "patch": {
    "name": "主标题",
    "transform": { "y": -24, "opacity": 0.95 },
    "physicsInteract": true
  }
}
```

```json
{
  "ok": true,
  "data": {
    "uuid": "0190f2c4-7a1e-7c3b-9d2a-1f4e6b8c0d12",
    "changed": ["name", "transform.y", "transform.opacity", "physicsInteract"],
    "entity": { "uuid": "0190f2c4-7a1e-7c3b-9d2a-1f4e6b8c0d12", "name": "主标题", "parentUuid": null, "start": 0, "end": 90, "duration": 90, "row": 1, "hidden": false, "locked": false, "contentKind": "text" }
  },
  "commit": { "hash": "...", "shortHash": "...", "message": "pvf: update entity \"主标题\"" }
}
```

### 3.7 `entity.updateMany` — 写

批量局部修改。**整批一个 commit，全成功或全回滚。**

**参数**：

| 字段 | 类型 | 必填 | 默认 | 说明 |
|---|---|---|---|---|
| `items` | array | **是** | — | 1..200 个 `{uuid, patch}` |

`patch` 字段同 `entity.update`。

**返回 `data`**：`{count, items:[{uuid,changed}], commit}`。

**错误**：同 `entity.update`；失败时 `details.index` 指向元素。

**示例**：

```json
{
  "items": [
    { "uuid": "<uuid-a>", "patch": { "hidden": true } },
    { "uuid": "<uuid-b>", "patch": { "transform": { "x": 120 } } }
  ]
}
```

### 3.8 `entity.move` — 写

在时间轴上平移实体，和/或改行号。**不改变时长，不改关键帧的相对帧号。**

**参数**：

| 字段 | 类型 | 必填 | 默认 | 说明 |
|---|---|---|---|---|
| `uuid` | string | **是** | — | 实体 UUID |
| `start` | integer | 二选一 | — | 新的全局 start；>= 0 |
| `delta` | integer | 二选一 | — | 平移帧数，可为负 |
| `row` | integer | 否 | 保持 | 新的行号 |

`start` 与 `delta` **必须且只能给一个**；`row` 可以单独给。至少要有一个实际变化。

**规则**：

- 时长 `end-start` 不变。
- **父实体平移会带着所有后代一起平移**；后代的 `start/end` 同步加 `delta`，关键帧和求解段相对帧号不动。
- 新范围必须仍落在父实体范围内，否则 `conflict`。
- 实体 start 不能小于 0。
- 改 `row` 只影响本实体在兄弟中的绘制顺序，不影响子实体。

**返回 `data`**：`{uuid, delta, start, end, row, childrenMoved:[uuid], commit}`。

**错误**：`invalid_argument`（`start`/`delta` 同时给或都没给、越界）、`conflict`（父级范围、锁定）、`permission_denied`。

**示例**：

```json
{ "uuid": "0190f2c4-7a1e-7c3b-9d2a-1f4e6b8c0d12", "delta": 30 }
```

```json
{
  "ok": true,
  "data": {
    "uuid": "0190f2c4-7a1e-7c3b-9d2a-1f4e6b8c0d12",
    "delta": 30,
    "start": 30,
    "end": 120,
    "row": 1,
    "childrenMoved": ["0190f2c4-...-child"]
  },
  "commit": { "hash": "...", "shortHash": "...", "message": "pvf: move entity \"标题\" 30f" }
}
```

### 3.9 `entity.trim` — 写

裁剪实体范围，并按规则移动/删除关键帧、调整 `sourceIn`、级联裁剪子实体。

**参数**：

| 字段 | 类型 | 必填 | 默认 | 说明 |
|---|---|---|---|---|
| `uuid` | string | **是** | — | 实体 UUID |
| `start` | integer | 否 | 保持 | 新的全局 start，>= 0 |
| `end` | integer | 否 | 保持 | 新的全局 end |
| `confirm` | boolean | 否 | false | 会删除子实体时必填 |

`start` 和 `end` 至少给一个；新范围必须 `end > start`。

**裁剪规则**：

| 操作 | 关键帧 | 视频/音频 `sourceIn` | 求解段 |
|---|---|---|---|
| start 右移 `d` | 所有关键帧减 `d`，小于 0 的删除 | `sourceIn += d` | 切进段内则删段，并把新 start 的解算位姿落成普通关键帧 |
| start 左移 `d` | 所有关键帧加 `d` | `sourceIn -= d`，不能小于 0 | 不变 |
| end 左移 | 删除 `frame >= 新 duration` 的关键帧 | 不变 | 结束标记改到新 clip 末帧（冻结在切点）；整段落在范围外则删除 |
| end 右移 | 已删除的关键帧不恢复 | 不能超过素材剩余长度 | 不恢复已删除的段 |

- start 右移如果把 `sourceIn` 推到素材末尾之外，返回 `conflict`。
- end 右移时 `sourceIn + (end-start) <= asset.durationFrames`，否则 `invalid_argument`。
- 子实体：越界部分被裁剪，越界关键帧删除；完全落在新范围外的子实体连同其后代删除。
- 删除子实体时必须 `confirm: true`；否则 `confirm_required`，`details.affected.deletedUuids` 列出会删掉的实体。
- 扩大父实体不会恢复已删除的内容。

**返回**：

```json
{
  "ok": true,
  "data": {
    "uuid": "...",
    "start": 10,
    "end": 90,
    "duration": 80,
    "sourceIn": 10,
    "removedKeyframes": 2,
    "removedSolvers": ["solver-uuid"],
    "deletedUuids": []
  },
  "commit": { "hash": "...", "shortHash": "...", "message": "pvf: trim entity \"标题\" [10,90)" }
}
```

**错误**：`invalid_argument`、`conflict`（素材长度、锁定、sourceIn 越界）、`confirm_required`、`permission_denied`。

**示例**：

```json
{ "uuid": "0190f2c4-7a1e-7c3b-9d2a-1f4e6b8c0d12", "start": 10 }
```

### 3.10 `entity.reparent` — 写

换父级。时间始终是绝对帧号，不因换父级改变。

**参数**：

| 字段 | 类型 | 必填 | 默认 | 说明 |
|---|---|---|---|---|
| `uuid` | string | **是** | — | 实体 UUID |
| `parentUuid` | string 或 null | **是** | — | 新父级；`null` 为根 |
| `mode` | string | **是** | — | `preserveWorld` 或 `keepLocal`，**没有默认值** |
| `row` | integer | 否 | 自动 | 新父级时间线里的行号 |

**模式**：

- `preserveWorld`：重算局部 `transform`，让实体在 `entity.start` 帧的世界位置不跳。父级有动画时，以 `entity.start` 帧的父世界变换为准。
- `keepLocal`：保持局部 `transform`，实体跳到新父级坐标系，画面可能跳。

**规则**：

- 子实体必须落在新父级 `[start,end)` 内，否则 `conflict`。
- 不能把实体挂到自己的后代下；检测到循环返回 `conflict`。
- 父级链最大 64 层。
- 新父级锁定则拒绝。
- 父级可以是任意实体，包括摄像机。

**返回 `data`**：`{uuid, parentUuid, mode, row, transform, commit}`。

**错误**：`invalid_argument`（mode 非法）、`conflict`（循环、范围、锁定）、`permission_denied`。

**示例**：

```json
{
  "uuid": "0190f2c4-7a1e-7c3b-9d2a-1f4e6b8c0d12",
  "parentUuid": "0190f2c4-...-parent",
  "mode": "preserveWorld"
}
```

### 3.11 `entity.duplicate` — 写

复制实体；默认连后代一起复制。新实体使用新 UUID。

**参数**：

| 字段 | 类型 | 必填 | 默认 | 说明 |
|---|---|---|---|---|
| `uuid` | string | **是** | — | 源实体 UUID |
| `deep` | boolean | 否 | true | 是否复制整棵子树 |
| `offsetFrames` | integer | 否 | 0 | 新实体整体平移帧数，可为负 |
| `parentUuid` | string 或 null | 否 | 源父级 | 新父级 |
| `name` | string | 否 | 源名 + `" 副本"` | 新根名称 |
| `row` | integer | 否 | 自动 | 新根行号 |

**规则**：

- 复制 `transform`、`content`、`keyframes`、`solvers`；求解段生成新 `id`，相对帧号不变。
- `deep: true` 时后代 UUID 全部重新生成，返回映射。
- 源实体锁定不影响复制（只读源）；但目标父级锁定会拒绝。
- 平移后必须落在目标父级范围内。
- 单次最多复制 200 个实体。

**返回**：

```json
{
  "ok": true,
  "data": {
    "sourceUuid": "...",
    "rootUuid": "...",
    "uuidMap": [{ "from": "...", "to": "..." }],
    "entityCount": 4
  },
  "commit": { "hash": "...", "shortHash": "...", "message": "pvf: duplicate entity \"卡片\"" }
}
```

**错误**：`invalid_argument`、`conflict`（范围、父级锁定）、`limit_exceeded`、`permission_denied`。

**示例**：

```json
{ "uuid": "0190f2c4-7a1e-7c3b-9d2a-1f4e6b8c0d12", "deep": true, "offsetFrames": 90 }
```

### 3.12 `entity.delete` — 写 · 需要 confirm

删除实体及其所有后代。**素材不自动删除。**

**参数**：

| 字段 | 类型 | 必填 | 默认 | 说明 |
|---|---|---|---|---|
| `uuid` | string | **是** | — | 实体 UUID |
| `confirm` | boolean | **是** | — | 必须为 `true` |

**规则**：

- 总是级联删除后代，没有“只删自己”的选项。
- 锁定实体（自己或祖先锁定）拒绝删除；先由人类解锁。
- 素材文件保留；返回 `orphanedAssets` 列出不再被引用的素材。
- 如果其他实体的 `orbit` / `blackhole` 求解段把被删实体当中心，这些求解段会被删除，并在 `disabledSolvers` 中返回。
- 删除父级后，后代的 UUID 全部失效。

**返回 `data`**：`{deletedUuids, orphanedAssets, disabledSolvers, commit}`。

**错误**：`confirm_required`、`invalid_argument`（UUID 不存在）、`conflict`（锁定）、`permission_denied`。

**示例**：

```json
{ "uuid": "0190f2c4-7a1e-7c3b-9d2a-1f4e6b8c0d12", "confirm": true }
```

```json
{
  "ok": true,
  "data": {
    "deletedUuids": ["0190f2c4-...", "0190f2c4-...-child"],
    "orphanedAssets": ["9f2c..."],
    "disabledSolvers": [{ "entityUuid": "0190f2c4-...", "solverId": "0190f2c4-...", "effect": "orbit" }]
  },
  "commit": { "hash": "...", "shortHash": "...", "message": "pvf: delete 2 entities" }
}
```

### 3.13 `entity.deleteMany` — 写 · 需要 confirm

批量删除；重复 UUID 和“祖先+后代同时出现”会先合并，只删一次。

**参数**：

| 字段 | 类型 | 必填 | 默认 | 说明 |
|---|---|---|---|---|
| `uuids` | array | **是** | — | 1..200 个 UUID |
| `confirm` | boolean | **是** | — | 必须为 `true` |

**返回 `data`**：`{deletedUuids, orphanedAssets, disabledSolvers, commit}`。

**错误**：`confirm_required`、`invalid_argument`（任一 UUID 不存在，`details.missing`）、`conflict`（任一锁定）、`permission_denied`。

**示例**：

```json
{ "uuids": ["<uuid-a>", "<uuid-b>"], "confirm": true }
```

---

## 4. keyframes

所有关键帧工具都按**单条轨道**操作，不会整表覆盖其他轨道。

### 4.1 `keyframes.get` — 只读

**参数**：

| 字段 | 类型 | 必填 | 默认 | 说明 |
|---|---|---|---|---|
| `uuid` | string | **是** | — | 实体 UUID |
| `track` | string | 否 | 省略 = 全部 | 轨道名 |

**返回 `data`**：

```json
{
  "uuid": "...",
  "duration": 90,
  "tracks": {
    "x": [{ "frame": 0, "value": 0, "easing": "linear" }],
    "opacity": [{ "frame": 12, "value": 1, "easing": "easeoutcubic" }]
  }
}
```

给 `track` 时只返回该轨道的 `keys`，以及 `duration`。

**错误**：`invalid_argument`（UUID 不存在、轨道未知，`details.allowedTracks`）。

**示例**：

```json
{ "uuid": "0190f2c4-7a1e-7c3b-9d2a-1f4e6b8c0d12", "track": "opacity" }
```

### 4.2 `keyframes.set` — 写

替换一条轨道的全部关键帧。

**参数**：

| 字段 | 类型 | 必填 | 默认 | 说明 |
|---|---|---|---|---|
| `uuid` | string | **是** | — | 实体 UUID |
| `track` | string | **是** | — | 轨道名，见 0.6 |
| `keys` | array | **是** | — | 关键帧数组；空数组 = 清空该轨道 |

每个 key 见 0.6 的“关键帧”。

**规则**：

- 帧号相对实体 `start`，必须是整数 `0..duration-1`；越界返回 `invalid_argument`，`details.allowedRange`。
- 同一轨道内帧号不能重复。
- 服务端会按 `frame` 升序排序后存储。
- `visible` / `physicsInteract` 用 step 插值；其他轨道按 `easing` 插值。
- 如果该轨道被求解段驱动，`keys` 必须包含段开始帧的关键帧（值可以改，会改变初始状态），且不能在 `[startFrame+1, endFrame]` 内出现关键帧；否则 `conflict`。
- `keyframes.set` 是**整条轨道替换**：没出现在 `keys` 里的旧关键帧会被删除。

**返回 `data`**：`{uuid, track, count, keys, commit}`。

**错误**：`invalid_argument`（范围、重复、未知轨道、值类型）、`conflict`（求解段冲突）、`permission_denied`。

**示例**：

```json
{
  "uuid": "0190f2c4-7a1e-7c3b-9d2a-1f4e6b8c0d12",
  "track": "opacity",
  "keys": [
    { "frame": 0, "value": 0, "easing": "linear" },
    { "frame": 12, "value": 1, "easing": "easeoutcubic" },
    { "frame": 75, "value": 1, "easing": "linear" },
    { "frame": 89, "value": 0, "easing": "easeincubic" }
  ]
}
```

### 4.3 `keyframes.setMany` — 写

一次替换多条轨道。**整批一个 commit，全成功或全回滚。**

**参数**：

| 字段 | 类型 | 必填 | 默认 | 说明 |
|---|---|---|---|---|
| `items` | array | **是** | — | 1..200 个 `{uuid, track, keys}` |

**规则**：

- 每个元素同 `keyframes.set`。
- 同一个 `(uuid, track)` 在同一批里出现两次是 `invalid_argument`。
- 先校验全部元素，再统一应用；任何失败整批回滚。

**返回**：

```json
{
  "ok": true,
  "data": {
    "count": 3,
    "items": [
      { "uuid": "...", "track": "opacity", "count": 4 },
      { "uuid": "...", "track": "y", "count": 2 }
    ]
  },
  "commit": { "hash": "...", "shortHash": "...", "message": "pvf: set keyframes (3 tracks)" }
}
```

**错误**：同 `keyframes.set`；`details.index` 指向失败元素。

**示例**：

```json
{
  "items": [
    {
      "uuid": "<标题文字>",
      "track": "opacity",
      "keys": [
        { "frame": 0, "value": 0 },
        { "frame": 12, "value": 1, "easing": "easeoutcubic" },
        { "frame": 89, "value": 0, "easing": "easeincubic" }
      ]
    },
    {
      "uuid": "<标题文字>",
      "track": "y",
      "keys": [
        { "frame": 0, "value": 40, "easing": "easeoutcubic" },
        { "frame": 15, "value": -10 }
      ]
    }
  ]
}
```

### 4.4 `keyframes.remove` — 写

删除指定帧上的一个关键帧。

**参数**：

| 字段 | 类型 | 必填 | 默认 | 说明 |
|---|---|---|---|---|
| `uuid` | string | **是** | — | 实体 UUID |
| `track` | string | **是** | — | 轨道名 |
| `frame` | integer | **是** | — | 相对帧号 |

**返回 `data`**：`{uuid, track, frame, removed: 0|1, commit}`。帧不存在时 `removed: 0`，不算错误（幂等）。

**错误**：`invalid_argument`（轨道未知、帧号越界）、`conflict`（删的是求解段开始帧）、`permission_denied`。

**示例**：

```json
{ "uuid": "0190f2c4-7a1e-7c3b-9d2a-1f4e6b8c0d12", "track": "opacity", "frame": 12 }
```

### 4.5 `keyframes.move` — 写

移动一个关键帧到另一帧。

**参数**：

| 字段 | 类型 | 必填 | 默认 | 说明 |
|---|---|---|---|---|
| `uuid` | string | **是** | — | 实体 UUID |
| `track` | string | **是** | — | 轨道名 |
| `fromFrame` | integer | **是** | — | 原相对帧号 |
| `toFrame` | integer | **是** | — | 目标相对帧号，`0..duration-1` |
| `overwrite` | boolean | 否 | false | 目标帧已有关键帧时是否覆盖 |

**规则**：

- `fromFrame` 不存在 → `invalid_argument`。
- `toFrame` 已有关键帧且 `overwrite: false` → `conflict`。
- 移动求解段开始帧、或把关键帧移进求解段驱动区间 → `conflict`。
- `fromFrame == toFrame` 是 no-op，返回 `moved: false`。

**返回 `data`**：`{uuid, track, fromFrame, toFrame, moved, replaced, commit}`。

**错误**：`invalid_argument`、`conflict`、`permission_denied`。

**示例**：

```json
{ "uuid": "0190f2c4-7a1e-7c3b-9d2a-1f4e6b8c0d12", "track": "opacity", "fromFrame": 12, "toFrame": 15, "overwrite": true }
```

### 4.6 `keyframes.clear` — 写

清空一条轨道的全部关键帧。

**参数**：

| 字段 | 类型 | 必填 | 默认 | 说明 |
|---|---|---|---|---|
| `uuid` | string | **是** | — | 实体 UUID |
| `track` | string | **是** | — | 轨道名 |

**返回 `data`**：`{uuid, track, removed, commit}`。

**错误**：`invalid_argument`（轨道未知）、`conflict`（该轨道被求解段驱动，先 `solver.delete`）、`permission_denied`。

**示例**：

```json
{ "uuid": "0190f2c4-7a1e-7c3b-9d2a-1f4e6b8c0d12", "track": "opacity" }
```

### 4.7 `keyframes.setEasing` — 写

修改一个已有关键帧的缓动。

**参数**：

| 字段 | 类型 | 必填 | 默认 | 说明 |
|---|---|---|---|---|
| `uuid` | string | **是** | — | 实体 UUID |
| `track` | string | **是** | — | 轨道名 |
| `frame` | integer | **是** | — | 相对帧号，必须已有关键帧 |
| `easing` | string 或 object | **是** | — | 预设名或贝塞尔对象 |

**返回 `data`**：`{uuid, track, frame, easing, commit}`。`easing` 是归一化后的值：预设返回字符串，贝塞尔返回对象。

**错误**：`invalid_argument`（关键帧不存在、缓动名未知、句柄非法）、`permission_denied`。

**示例**：

```json
{
  "uuid": "0190f2c4-7a1e-7c3b-9d2a-1f4e6b8c0d12",
  "track": "opacity",
  "frame": 12,
  "easing": { "type": "bezier", "inX": 0.22, "inY": 0.61, "outX": 0.36, "outY": 1 }
}
```

**注意**：缓动挂在“到达的关键帧”上；改第 12 帧的缓动影响第 0→12 帧这一段。`visible` / `physicsInteract` 是 step 轨道，设置缓动没有视觉效果。

---

## 5. solver

### 5.1 求解段规则

- 一个实体只有一条时间轴；求解段按时间顺序排列，**不能重叠**。段是闭区间 `[startFrame, endFrame]`；下一段必须从 `endFrame + 1` 或更晚开始。
- 一个求解段只装**一个效果**：`gravity` / `orbit` / `spin` / `blackhole` / `spiral`。
- **开始帧带值**：`solver.create` 要求开始帧在受驱动轨道上已有关键帧。
  - `gravity` / `orbit` / `blackhole` / `spiral`：`x` 和 `y` 都必须在 `startFrame` 有关键帧。
  - `spin`：`rotation` 必须在 `startFrame` 有关键帧。
- **结束帧是标记，不存值**：解算到 `endFrame` 后速度归零、值保持；没有结束帧时（`endFrame = -1`）一直解到 clip 最后一帧。只有时间上最后一个求解段允许省略结束帧；要在它后面再建段，必须先补结束帧。
- 段内受驱动轨道锁定：不能在 `[startFrame+1, endFrame]` 加普通关键帧；结束帧之后才能继续普通关键帧。
- 段结束后接普通关键帧：结束帧的解算位姿作为隐式左值，下一个普通关键帧从它开始插值，不能跳回段前旧值或默认值。
- 被驱动属性：
  - `gravity` / `orbit` / `blackhole` / `spiral`：`x`、`y`
  - `spin`：`rotation`
  - 其他属性仍可用普通关键帧。
- 移动实体：求解段随实体平移，相对帧号和参数不变。
- 裁剪 start 切进段内：整段删除（start/end 标记都移除），同时把新 start 处的解算位姿落成普通关键帧，受驱动属性从该值继续，避免瞬移。
- 裁剪 end 切进段内：把结束标记改到新 clip 末帧（冻结在切点），不要改成 `-1`；之后再拉长 clip 不会让物理段复活。整段落在新范围外则删除。
- 创建求解段时，如果区间内已有被驱动属性的普通关键帧，直接拒绝并列出冲突帧；不静默删除、不静默覆盖。
- 预览和导出使用同一套固定步长解算（`1/fps`），确定性、无随机；段上存 `version`，保证结果可复现。
- `solver.evaluate` 只读，不写关键帧、不做烘焙。

### 5.2 效果参数（可直接用作 UI tooltip）

所有 `params` 都是**每个效果独立**的对象；未知字段返回 `invalid_argument`。`solver.update` 的 `params` 是**浅合并**：只改给出的键；把某个键设为 `null` 表示恢复默认值。

#### gravity

| 参数 | 类型 | 单位 | 默认 | 范围 | 用途（tooltip） |
|---|---|---|---|---|---|
| `targetUuid` | string | — | 必填 | 已存在实体 | 重力地面/方向实体；它的局部 +Y 是重力方向，目标旋转时方向跟着转。 |
| `strength` | number | px/s² | 980 | 0..1000000 | 重力加速度。数值越大下落越快；方向始终是目标实体的局部 +Y，实体旋转时重力方向跟着转。 |
| `elasticity` | number | 比例 | 0.35 | 0..1 | 碰撞反弹保留的法向速度比例。0 = 完全不反弹，1 = 无能量损失。 |
| `friction` | number | 比例 | 0.2 | 0..1 | 碰撞时削减的切向速度比例。0 = 无摩擦、沿地面滑行；1 = 立即停止切向运动。 |
| `stopThreshold` | number | px/s | 2 | 0..1000 | 速度低于该值就停止并保持当前值，避免无限微小抖动。 |

段级字段：

| 字段 | 类型 | 单位 | 默认 | 用途（tooltip） |
|---|---|---|---|---|
| `initialVelocity.x` | number | px/s | 0 | 开始帧的求解空间水平初速度；轨迹预览里可用手柄拖。 |
| `initialVelocity.y` | number | px/s | 0 | 开始帧的求解空间垂直初速度；负值向上。 |

碰撞：目标实体与所有 `physicsInteract = 1` 的实体碰撞（排除自己）。矢量用实际路径，图片/文字/HTML 用包围盒。重力方向是**目标实体**的局部 +Y；目标实体旋转时重力方向跟着转，运动实体自己的 rotation 不影响重力方向。

#### orbit

| 参数 | 类型 | 单位 | 默认 | 范围 | 用途（tooltip） |
|---|---|---|---|---|---|
| `targetUuid` | string | — | 必填 | 已存在实体 | 公转中心实体。每帧读取它当前的位置和旋转；不能是目标实体自己，也不能形成循环依赖。 |
| `aspect` | number | 比例 | 1.0 | 0.01..1 | 椭圆的短轴/长轴。1 = 正圆；越小轨道越扁。 |
| `angle` | number | deg | 0 | -360..360 | 椭圆长轴相对中心实体局部坐标的旋转角。 |
| `direction` | integer | — | 1 | `1` 或 `-1` | 公转方向。`1` 顺时针，`-1` 逆时针（y 向下坐标系）。 |
| `periodFrames` | integer | 帧 | 60 | 1..1000000 | 转一圈所需帧数。 |

- 椭圆必定经过开始帧的位置；大小由“中心到起点距离”反解，实际长半轴/短半轴在 `solver.list` / `solver.evaluate` 的 `computed` 里只读返回。
- 起点决定初始相位；周期从起点开始算。
- 中心可以是任意实体；每帧取它的当前位置和旋转。

#### spin

| 参数 | 类型 | 单位 | 默认 | 范围 | 用途（tooltip） |
|---|---|---|---|---|---|
| `angularVelocity` | number | deg/帧 | 6 | 0..360 | 每帧旋转角度。30fps 下 6 = 180°/秒。 |
| `direction` | integer | — | 1 | `1` 或 `-1` | 旋转方向。`1` 顺时针，`-1` 逆时针。 |

- 只驱动 `rotation`，不改 `x/y`。
- 轴心用实体 `pivot`；初始 rotation 取开始帧关键帧的值。
- 不修改实体大小、位置或可见性。

#### blackhole

| 参数 | 类型 | 单位 | 默认 | 范围 | 用途（tooltip） |
|---|---|---|---|---|---|
| `targetUuid` | string | — | 必填 | 已存在实体 | 黑洞实体。每帧读取它的位置；不能是目标实体自己。 |
| `simulationSpeed` | number | 倍率 | 1 | 0.01..20 | 模拟时间倍率。> 1 加速演化，< 1 放慢；只影响解算，不改工程帧率。 |
| `mass` | number | 相对单位 | 1000 | > 0..1000000000 | 黑洞质量。越大引力越强、捕获范围越大。 |
| `angularMomentum` | number | 相对单位 | 0 | -1000000..1000000 | 角动量。非零时产生切向拖拽，轨迹呈旋涡；正负决定旋涡方向。 |
| `eventHorizonRadius` | number | px | 80 | 0..1000000 | 事件视界半径，用于可视化和捕获判定参考。它不是硬边界，物体可以穿过后继续运动。 |
| `objectRadius` | number | px | 40 | 0..1000000 | 运动物体半径，用于和视界/其他物体做距离判定。 |
| `softening` | number | px | 60 | > 0..1000000 | 引力软化半径，避免距离趋近 0 时力发散；越大中心附近越平缓。 |
| `maxSpeed` | number | px/s | 4000 | > 0..1000000 | 速度上限，防止数值爆炸或物体瞬间飞出画布。 |

段级字段：

| 字段 | 类型 | 单位 | 默认 | 用途（tooltip） |
|---|---|---|---|---|
| `initialVelocity.x` | number | px/s | 0 | 开始帧的求解空间水平初速度。 |
| `initialVelocity.y` | number | px/s | 0 | 开始帧的求解空间垂直初速度。 |

- 只改 `x/y`；不改大小、rotation、可见性。
- 速度够大可以逃离黑洞；事件视界不是硬边界。

#### spiral

| 参数 | 类型 | 单位 | 默认 | 范围 | 用途（tooltip） |
|---|---|---|---|---|---|
| `targetUuid` | string | — | 必填 | 已存在实体 | 螺旋中心实体。每帧读取它的位置；不能是目标实体自己。 |
| `turns` | number | 圈 | 3 | 0..1000 | 总圈数；`speedDegPerSec = 0` 时使用。 |
| `direction` | integer | — | 1 | `1` 或 `-1` | 旋转方向。**`1` 逆时针，`-1` 顺时针**（Y-up 正角逆时针；注意与 orbit/spin 的约定相反）。 |
| `aspect` | number | 短轴/长轴 | 1 | 0.01..1 | 椭圆的短轴/长轴。1 = 正圆；越小轨道越扁。 |
| `angle` | number | deg | 0 | -360..360 | 椭圆长轴相对中心实体局部坐标的旋转角。 |
| `speedDegPerSec` | number | deg/s | 0 | ≥ 0 | > 0 时总角度 = 速度 × 段时长，覆盖 `turns`。 |

- 只驱动 `x/y`；rotation 仍走普通关键帧。
- 起始半径/角度由段起点相对目标在 `startFrame` 的位置决定；椭圆半轴线性缩到 0，最后一帧精确落在目标中心。
- 中心跟随目标动画；纯运动学，无碰撞/物理停止；自动结束为 `ClipEnd`，显式 `endFrame` 为 `ExplicitEnd`。
- `initialVelocity` 对 spiral 无效，传入返回 `invalid_argument`。

### 5.3 `solver.create` — 写

**参数**：

| 字段 | 类型 | 必填 | 默认 | 说明 |
|---|---|---|---|---|
| `entityUuid` | string | **是** | — | 目标实体 UUID |
| `effect` | string | **是** | — | `gravity` / `orbit` / `spin` / `blackhole` / `spiral` |
| `startFrame` | integer | **是** | — | 相对实体 start，`0..duration-1` |
| `endFrame` | integer | 否 | -1 | 相对帧号，`> startFrame` 且 `<= duration-1`；`-1` = clip 末帧 |
| `initialVelocity` | object | 否 | `{x:0,y:0}` | px/s，仅 `gravity` / `blackhole` |
| `params` | object | **是** | — | 按效果取字段，见 5.2 |

**规则**：

- 开始帧必须已有关键帧：`gravity`/`orbit`/`blackhole`/`spiral` 需要 `x` 和 `y`；`spin` 需要 `rotation`。缺失返回 `conflict`，`details.missingTracks`。
- 新段不能与已有段重叠；重叠返回 `conflict`，`details.conflicts`。
- `initialVelocity` 给 `orbit` / `spin` / `spiral` 是 `invalid_argument`。
- 创建成功只新增段，不修改开始帧关键帧的值。
- 一次调用一个 commit。

**返回**：

```json
{
  "ok": true,
  "data": {
    "solver": {
      "id": "0190f2c4-8b2e-7d4c-a1b3-2c5d7e9f0a13",
      "effect": "gravity",
      "startFrame": 0,
      "endFrame": 119,
      "version": 1,
      "initialVelocity": { "x": 540, "y": -240 },
      "params": { "strength": 980, "elasticity": 0.45, "friction": 0.2, "stopThreshold": 3 }
    }
  },
  "commit": { "hash": "...", "shortHash": "...", "message": "pvf: create solver \"小球\" gravity" }
}
```

**错误**：`invalid_argument`（帧号、参数、效果未知）、`conflict`（重叠、缺开始关键帧、锁定）、`permission_denied`、`not_available`（解算器版本不支持）。

**示例**：

```json
{
  "entityUuid": "0190f2c4-7a1e-7c3b-9d2a-1f4e6b8c0d12",
  "effect": "gravity",
  "startFrame": 0,
  "endFrame": 119,
  "initialVelocity": { "x": 540, "y": -240 },
  "params": { "strength": 980, "elasticity": 0.45, "friction": 0.2, "stopThreshold": 3 }
}
```

### 5.4 `solver.update` — 写

修改一个求解段。`effect` 不可改；要换效果先 `solver.delete` 再 `solver.create`。

**参数**：

| 字段 | 类型 | 必填 | 默认 | 说明 |
|---|---|---|---|---|
| `entityUuid` | string | **是** | — | 目标实体 UUID |
| `solverId` | string | **是** | — | 求解段 id |
| `patch` | object | **是** | — | `startFrame`、`endFrame`、`initialVelocity`、`params` 的任意子集 |

**规则**：

- 改 `startFrame` 后，新开始帧必须有关键帧；旧开始帧的关键帧保留，变成普通关键帧。
- 改后的段仍不能与任何段重叠。
- `params` 浅合并；`null` 恢复默认。
- `initialVelocity` 整体替换，缺失的分量按 0。
- 段内受驱动轨道仍保持锁定。

**返回 `data`**：`{solver, commit}`。

**错误**：`invalid_argument`、`conflict`（重叠、缺关键帧、锁定）、`permission_denied`。

**示例**：

```json
{
  "entityUuid": "0190f2c4-7a1e-7c3b-9d2a-1f4e6b8c0d12",
  "solverId": "0190f2c4-8b2e-7d4c-a1b3-2c5d7e9f0a13",
  "patch": { "params": { "strength": 1400, "elasticity": 0.55 } }
}
```

### 5.5 `solver.delete` — 写

删除一个求解段。开始帧的关键帧**保留**，之后作为普通关键帧参与求值。

**参数**：

| 字段 | 类型 | 必填 | 默认 | 说明 |
|---|---|---|---|---|
| `entityUuid` | string | **是** | — | 目标实体 UUID |
| `solverId` | string | **是** | — | 求解段 id |

**返回 `data`**：`{deleted: true, solverId, commit}`。

**错误**：`invalid_argument`（段不存在）、`conflict`（锁定）、`permission_denied`。

**示例**：

```json
{ "entityUuid": "0190f2c4-7a1e-7c3b-9d2a-1f4e6b8c0d12", "solverId": "0190f2c4-8b2e-7d4c-a1b3-2c5d7e9f0a13" }
```

### 5.6 `solver.list` — 只读

**参数**：

| 字段 | 类型 | 必填 | 默认 | 说明 |
|---|---|---|---|---|
| `entityUuid` | string | **是** | — | 目标实体 UUID |

**返回 `data`**：`{entityUuid, duration, solvers:[...]}`，按 `startFrame` 升序。

每个 solver 是求解段对象；`orbit` 额外带只读的 `computed`：

```json
{
  "computed": {
    "targetUuid": "...",
    "centerX": 0,
    "centerY": 0,
    "semiMajor": 240,
    "semiMinor": 120,
    "aspect": 0.5,
    "angle": 0,
    "periodFrames": 60
  }
}
```

`semiMajor` / `semiMinor` 是当前解算反解出的长半轴/短半轴，单位 px，只读。

**错误**：`invalid_argument`（UUID 不存在）。

**示例**：

```json
{ "entityUuid": "0190f2c4-7a1e-7c3b-9d2a-1f4e6b8c0d12" }
```

### 5.7 `solver.evaluate` — 只读

在内存里解算，返回采样点；不写关键帧、不产生 commit。

**参数**：

| 字段 | 类型 | 必填 | 默认 | 说明 |
|---|---|---|---|---|
| `entityUuid` | string | **是** | — | 目标实体 UUID |
| `solverId` | string | 否 | 全部 | 只解算指定段 |
| `frames` | array | 否 | 段内每一帧 | 相对帧号数组；最多 10000 个采样 |

**返回 `data`**：

```json
{
  "entityUuid": "...",
  "framesRelativeTo": "entityStart",
  "results": [
    {
      "solverId": "...",
      "effect": "gravity",
      "samples": [
        { "frame": 0, "x": -360, "y": -360, "rotation": 0, "velocity": { "x": 540, "y": -240 }, "collided": false, "stopped": false }
      ],
      "summary": {
        "minX": -360,
        "maxX": 560,
        "minY": -420,
        "maxY": 280,
        "firstContactFrame": 57,
        "stoppedFrame": null
      }
    }
  ]
}
```

- `x` / `y` 是实体局部坐标，`rotation` 是度，`velocity` 是 px/s 局部速度。
- `collided` 在发生碰撞的采样帧为 `true`；`stopped` 在速度低于阈值或被结束标记冻结时为 `true`。
- `summary` 字段按效果可选；`orbit` 会带 `computed` 的只读轨道参数。
- 目标帧不在任何求解段内时返回 `invalid_argument`。
- 采样超过 10000 个时返回 `limit_exceeded`，请显式给 `frames`。

**错误**：`invalid_argument`（帧越界、段不存在）、`conflict`（开始帧关键帧缺失）、`limit_exceeded`、`not_available`（版本不支持）。

**示例**：

```json
{
  "entityUuid": "0190f2c4-7a1e-7c3b-9d2a-1f4e6b8c0d12",
  "solverId": "0190f2c4-8b2e-7d4c-a1b3-2c5d7e9f0a13",
  "frames": [0, 10, 20, 30, 40, 50, 57, 60, 80, 100, 119]
}
```

---

## 6. asset

素材没有 UUID，只有 sha256 内容哈希。相同内容重复导入会去重。

### 6.1 `asset.import` — 写

把本地文件/目录复制进工程 `assets/<sha256>/`。

**参数**：

| 字段 | 类型 | 必填 | 默认 | 说明 |
|---|---|---|---|---|
| `path` | string | **是** | — | 服务器本地绝对路径；文件、`.pvhtml` 目录或 zip |
| `name` | string | 否 | 文件名 | 素材显示名，1..256 字符 |
| `type` | string | 否 | 自动检测 | `image` / `video` / `audio` / `html` |
| `extractAudio` | boolean | 否 | 视频为 true | 视频是否拆出无损 WAV 音频素材 |
| `confirm` | boolean | 否 | false | 路径在工程目录外时必须为 `true` |

**规则**：

- 只接受本地路径，不接受 URL、`~`、相对路径。
- **工程目录外的路径需要 `confirm: true`**；工程内路径可直接导入。软链指向工程外也按工程外处理。
- 素材文件进工程 `assets/`，但**不进 git**；纯素材导入没有文本 diff 时可能没有 commit。
- 视频设置 `extractAudio: true` 时同时写入视频素材和 WAV 音频素材；返回两个 hash。
- `ffmpeg` 缺失且 `extractAudio: true` → `not_available`，整次导入回滚，不留半个素材。
- 同一 sha256 已存在时返回已有素材，`deduplicated: true`；不会改已有素材的名字。
- HTML 素材是 `.pvhtml` 目录包或 zip；包内必须有 `index.html`。
- 音频素材使用同一个内容寻址；视频拆出的 WAV 也参与去重。
- 导入视频后宿主可以后台抽 WebP 图片序列，抽图不属于 MCP 调用/commit。

**返回**：

```json
{
  "ok": true,
  "data": {
    "asset": { "hash": "<videoHash>", "type": "video", "name": "片头", "durationFrames": 240, "width": 1920, "height": 1080 },
    "audioAsset": { "hash": "<audioHash>", "type": "audio", "name": "片头 音频", "durationFrames": 240, "sampleRate": 48000, "channels": 2 },
    "deduplicated": false,
    "extractedAudio": true
  },
  "commit": { "hash": "...", "shortHash": "...", "message": "pvf: import asset \"片头\"" }
}
```

没有拆音频时 `audioAsset` 为 `null`，`extractedAudio` 为 `false`。

**错误**：`invalid_argument`（路径不存在、类型不支持、HTML 缺 `index.html`）、`not_available`（ffmpeg 缺失）、`limit_exceeded`（超过服务端文件大小上限）、`permission_denied`、`internal_error`。

**示例**：

```json
{
  "path": "/home/user/media/opening.mp4",
  "name": "片头",
  "extractAudio": true
}
```

### 6.2 `asset.list` — 只读

**参数**：

| 字段 | 类型 | 必填 | 默认 | 说明 |
|---|---|---|---|---|
| `type` | string | 否 | 全部 | `image` / `video` / `audio` / `html` |
| `query` | string | 否 | — | 按名称子串匹配，不区分大小写 |
| `offset` | integer | 否 | 0 | >= 0 |
| `limit` | integer | 否 | 100 | 1..500 |

**返回 `data`**：`{assets:[素材对象], page}`。

**错误**：`invalid_argument`（类型未知、分页非法）。

**示例**：

```json
{ "type": "video", "query": "片头", "limit": 20 }
```

### 6.3 `asset.get` — 只读

**参数**：

| 字段 | 类型 | 必填 | 默认 | 说明 |
|---|---|---|---|---|
| `hash` | string | **是** | — | 64 位 sha256 |

**返回 `data`**：素材对象，含 `usedBy` 和 `missing`。

**错误**：`invalid_argument`（hash 格式非法或素材不存在）。

**示例**：

```json
{ "hash": "9f2c..." }
```

### 6.4 `asset.assign` — 写

给实体换素材/内容。

**参数**：

| 字段 | 类型 | 必填 | 默认 | 说明 |
|---|---|---|---|---|
| `entityUuid` | string | **是** | — | 实体 UUID |
| `assetHash` | string | **是** | — | 目标素材 hash |
| `sourceIn` | integer | 否 | 保留旧值；没有则 0 | 视频/音频源偏移 |
| `volume` | number | 否 | 保留旧值；没有则 1.0 | 仅音频 |

**兼容组**：

| 目标实体当前 kind | 允许的素材组 |
|---|---|
| `empty` | 任意 |
| `asset`（视觉组） | `image` / `video` / `html` |
| `asset`（音频组） | `audio` |
| `text` / `vector` | 不允许；先用 `entity.update` 改成 `empty`，再 `asset.assign` |
| `camera` | 不允许；先用 `entity.update` 改成 `empty`，再 `asset.assign` |

**时长调整**：

- 新素材是 `image` / `html`：保持实体当前时长。
- 新素材是 `video` / `audio` 且比实体短：实体 `end` 自动裁到 `start + (asset.durationFrames - sourceIn)`，越界关键帧删除。
- 新素材更长：不自动拉长实体。
- `sourceIn` 会被夹到 `0..max(0, asset.durationFrames-1)`，保证 `sourceIn + duration <= asset.durationFrames`。

**返回**：

```json
{
  "ok": true,
  "data": {
    "entity": { "uuid": "...", "name": "片头视频", "start": 120, "end": 330, "duration": 210, "row": 0, "hidden": false, "locked": false, "contentKind": "asset" },
    "asset": { "hash": "...", "type": "video", "name": "片头", "durationFrames": 240 },
    "adjusted": { "start": 120, "end": 330, "duration": 210, "sourceIn": 30, "removedKeyframes": 0 }
  },
  "commit": { "hash": "...", "shortHash": "...", "message": "pvf: assign asset \"片头\" to \"片头视频\"" }
}
```

**错误**：`invalid_argument`（素材不存在、hash 非法）、`conflict`（不兼容组、摄像机、素材太短、锁定）、`permission_denied`。

**示例**：

```json
{
  "entityUuid": "0190f2c4-7a1e-7c3b-9d2a-1f4e6b8c0d12",
  "assetHash": "9f2c...",
  "sourceIn": 30
}
```

### 6.5 `asset.unassign` — 写

把实体的内容清空为 `empty`，保留 UUID、时间、行、变换、关键帧和子实体。

**参数**：

| 字段 | 类型 | 必填 | 默认 | 说明 |
|---|---|---|---|---|
| `entityUuid` | string | **是** | — | 实体 UUID |

**返回 `data`**：`{entity, previousAssetHash, commit}`。

**错误**：`invalid_argument`（实体不存在、当前不是 asset）、`conflict`（锁定）、`permission_denied`。

**示例**：

```json
{ "entityUuid": "0190f2c4-7a1e-7c3b-9d2a-1f4e6b8c0d12" }
```

**注意**：类型专用轨道（例如 `volume`、`width`）会保留在实体文件里但暂不生效；换回对应内容类型后继续使用。

### 6.6 `asset.rename` — 写

**参数**：

| 字段 | 类型 | 必填 | 默认 | 说明 |
|---|---|---|---|---|
| `hash` | string | **是** | — | 素材 hash |
| `name` | string | **是** | — | 新名称，1..256 字符 |

**返回 `data`**：`{hash, name, commit}`。

**错误**：`invalid_argument`、`permission_denied`。

**示例**：

```json
{ "hash": "9f2c...", "name": "片头 v2" }
```

### 6.7 `asset.setTags` — 写

设置素材标签；传空数组清除。

**参数**：

| 字段 | 类型 | 必填 | 默认 | 说明 |
|---|---|---|---|---|
| `hash` | string | **是** | — | 素材 hash |
| `tags` | array | **是** | — | 字符串数组；最多 32 个，单个 1..64 字符 |

**规则**：

- 标签自动 trim、去重、丢弃空白项。
- 超过 32 个或单标签超过 64 字符 → `invalid_argument`，不改素材。
- 标签存在工程 `assets/<hash>/asset.json`，不进 git。

**返回 `data`**：素材对象（含新的 `tags`）。

**错误**：`invalid_argument`、`permission_denied`。

**示例**：

```json
{ "hash": "9f2c...", "tags": ["片头", "season-1"] }
```

### 6.8 `asset.delete` — 写 · 需要 confirm

删除素材文件。**不删除引用它的实体。**

**参数**：

| 字段 | 类型 | 必填 | 默认 | 说明 |
|---|---|---|---|---|
| `hash` | string | **是** | — | 素材 hash |
| `confirm` | boolean | **是** | — | 必须为 `true` |

**规则**：

- 引用该素材的实体保留，显示“素材缺失”占位。
- 返回 `missingEntities`，列出受影响的实体。
- 重新导入相同内容会自动恢复这些实体，因为 hash 不变。
- 删除不可恢复；需要保留时先复制文件。

**返回 `data`**：`{hash, deleted: true, missingEntities:[uuid], commit}`。

**错误**：`confirm_required`、`invalid_argument`（素材不存在）、`permission_denied`。

**示例**：

```json
{ "hash": "9f2c...", "confirm": true }
```

---

## 7. markers

标记是全局标注，不占行、不渲染、不影响时间轴长度。帧号是**全局帧号**。

### 7.1 `markers.list` — 只读

**参数**：

| 字段 | 类型 | 必填 | 默认 | 说明 |
|---|---|---|---|---|
| `fromFrame` | integer | 否 | 0 | 起始全局帧，包含 |
| `toFrame` | integer | 否 | 无穷 | 结束全局帧，包含 |
| `offset` | integer | 否 | 0 | >= 0 |
| `limit` | integer | 否 | 100 | 1..500 |

**返回 `data`**：`{markers:[{id,frame,title,note,tags,color}], page}`，按 `frame` 升序。

**错误**：`invalid_argument`。

**示例**：

```json
{ "fromFrame": 0, "toFrame": 300 }
```

### 7.2 `markers.set` — 写

创建或更新标记。

**参数**：

| 字段 | 类型 | 必填 | 默认 | 说明 |
|---|---|---|---|---|
| `id` | string | 否 | 省略 = 新建 | 已有标记 id；提供时必须存在 |
| `frame` | integer | **是** | — | 全局帧号，>= 0 |
| `title` | string | **是** | — | 1..256 字符 |
| `note` | string | 否 | `""` | Markdown 备注，最长 4096 |
| `tags` | array | 否 | `[]` | 字符串数组，最多 32 个，每个 <= 64 字符 |
| `color` | string | 否 | `#ffbe3c` | CSS hex |

**返回 `data`**：`{marker, created, commit}`。

**错误**：`invalid_argument`（id 不存在、帧/标题/颜色非法）、`permission_denied`。

**示例**：

```json
{
  "frame": 48,
  "title": "副歌",
  "note": "音乐进入副歌，标题在这里完全展开。",
  "tags": ["music", "chorus"],
  "color": "#ff5c8a"
}
```

### 7.3 `markers.remove` — 写

**参数**：

| 字段 | 类型 | 必填 | 默认 | 说明 |
|---|---|---|---|---|
| `id` | string | **是** | — | 标记 id |

**返回 `data`**：`{removed: true, id, commit}`。

**错误**：`invalid_argument`（id 不存在）、`permission_denied`。

**示例**：

```json
{ "id": "0190f2c4-5c6d-7e8f-9a0b-1c2d3e4f5a6b" }
```

---

## 8. preview 与 view

### 8.1 `preview.render_frame` — 只读

渲染指定帧为 PNG，并返回当前 UI 状态。

**参数**：

| 字段 | 类型 | 必填 | 默认 | 说明 |
|---|---|---|---|---|
| `frame` | integer | 否 | 当前播放头 | 全局帧号，>= 0 |
| `cameraUuid` | string 或 null | 否 | 当前活动摄像机 | `null` 显式表示整画布；省略表示跟随 UI |
| `width` | integer | 否 | 工程宽度 | 16..4096 |
| `height` | integer | 否 | 工程高度 | 16..4096 |
| `includeBase64` | boolean | 否 | false | 是否在返回里塞 PNG base64 |

`width`/`height` 只给一个时按工程宽高比自动补另一个。

**返回 `data`**：

```json
{
  "path": "/home/user/.cache/pvforge/preview/f00045.png",
  "mimeType": "image/png",
  "width": 1280,
  "height": 720,
  "frame": 45,
  "cameraUuid": "0190f2c4-...",
  "playheadFrame": 45,
  "selectedUuids": ["0190f2c4-..."],
  "openTimelineOwnerUuid": null,
  "view": { "startFrame": 0, "endFrame": 90, "scale": 1.0 },
  "base64": "..."
}
```

- `path` 是服务器本地路径；同机客户端可直接读文件，跨机客户端用 `includeBase64: true`。
- `base64` 只在 `includeBase64: true` 时出现；大图会显著增大响应，按需使用。
- 没有摄像机且没有指定 `cameraUuid` 时，渲染整个画布；导出仍然要求摄像机。
- HTML 素材找不到 Chrome 时，HTML 层显示错误占位，其他层正常渲染，调用仍成功。
- 服务端限流约 **2 次/秒**；超限返回 `limit_exceeded`。
- 不产生 commit。
- 建议每次修改后只抽查 3–5 个关键帧，不要连续狂刷。

**错误**：`invalid_argument`（帧/尺寸/摄像机非法）、`limit_exceeded`（限流）、`render_failed`、`render_timeout`、`not_available`。

**示例**：

```json
{ "frame": 45, "width": 1280, "includeBase64": false }
```

### 8.2 `view.state` — 只读

只返回 UI 状态，不渲染图片。

**参数**：无。

**返回 `data`**：

| 字段 | 类型 | 说明 |
|---|---|---|
| `playheadFrame` | integer | 当前播放头，全局帧号 |
| `selectedUuids` | array | 当前选中实体 |
| `openTimelineOwnerUuid` | string 或 null | 当前打开的私有时间线 owner；`null` 主时间线 |
| `view` | object | `{startFrame,endFrame,scale}` |
| `activeCameraUuid` | string 或 null | 当前活动摄像机 |
| `writeEnabled` | boolean | AI 写权限是否开启 |
| `stopped` | boolean | 是否被急停 |
| `entityCount` | integer | 实体总数 |

**错误**：`not_available`。

**示例**：

```json
{}
```

```json
{
  "ok": true,
  "data": {
    "playheadFrame": 45,
    "selectedUuids": ["0190f2c4-7a1e-7c3b-9d2a-1f4e6b8c0d12"],
    "openTimelineOwnerUuid": null,
    "view": { "startFrame": 0, "endFrame": 90, "scale": 1.0 },
    "activeCameraUuid": null,
    "writeEnabled": true,
    "stopped": false,
    "entityCount": 12
  }
}
```

---

## 9. schema 与 guide

### 9.1 `schema.get` — 只读

返回数据对象的 JSON Schema（不是工具入参 schema；工具入参看 `tools/list`）。

**参数**：

| 字段 | 类型 | 必填 | 默认 | 说明 |
|---|---|---|---|---|
| `topic` | string | 否 | `all` | `project` / `entity` / `keyframes` / `solver` / `asset` / `markers` / `preview` / `all` |

**返回 `data`**：`{version, topic, schema}`。`topic: "all"` 时 `schema` 是以 topic 为键的对象。

**错误**：`invalid_argument`（topic 未知）。

**示例**：

```json
{ "topic": "solver" }
```

### 9.2 `guide.get` — 只读

返回 Markdown 指南。

**参数**：

| 字段 | 类型 | 必填 | 默认 | 说明 |
|---|---|---|---|---|
| `topic` | string | 否 | `authoring` | `authoring` / `workflow` / `solver` / `assets` / `examples` |

**返回 `data`**：`{topic, markdown}`。

**错误**：`invalid_argument`（topic 未知）。

**示例**：

```json
{ "topic": "solver" }
```

---

## 10. history

撤销分两层：

- **内存 undo/redo（热）**：文本数据快照，Ctrl+Z/Ctrl+Shift+Z 直接恢复，不产生 git commit。
- **git 历史（冷）**：AI 每次写工具调用立即 commit；人类停手 1 秒/保存时合并 commit。跨会话只能靠 git 历史恢复。
- 素材文件不在 undo 范围；素材不进 git，靠用户备份。

### 10.1 `history.undo` — 写（内存撤销）

**参数**：

| 字段 | 类型 | 必填 | 默认 | 说明 |
|---|---|---|---|---|
| `steps` | integer | 否 | 1 | 1..10，回退最近几步内存操作 |

**规则**：

- 直接应用内存 before 快照；不调用 git revert、不产生 commit。
- 覆盖实体、工程设置、关键帧、求解段、markers 等文本数据。
- 素材文件不改。
- 无记录 → `conflict`。
- 成功后返回当前 undo/redo 深度和 HEAD（HEAD 不变）。

**返回 `data`**：

```json
{
  "steps": 1,
  "undoDepth": 4,
  "redoDepth": 1,
  "head": "a1b2c3d..."
}
```

**错误**：`conflict`（无可撤销）、`permission_denied`、`internal_error`。

### 10.2 `history.redo` — 写（内存重做）

**参数**：

| 字段 | 类型 | 必填 | 默认 | 说明 |
|---|---|---|---|---|
| `steps` | integer | 否 | 1 | 1..10，重做最近几步 |

**规则**：

- 直接应用内存 after 快照；不产生 commit。
- 无记录 → `conflict`。

**返回 `data`**：同 `history.undo`，字段为 `redoDepth` 等。

**错误**：同 `history.undo`。

### 10.3 `history.log` — 只读

**参数**：

| 字段 | 类型 | 必填 | 默认 | 说明 |
|---|---|---|---|---|
| `offset` | integer | 否 | 0 | >= 0 |
| `limit` | integer | 否 | 20 | 1..100 |

**返回 `data`**：

```json
{
  "commits": [
    {
      "hash": "a1b2c3d4e5f60718293a4b5c6d7e8f9012345678",
      "shortHash": "a1b2c3d",
      "message": "pvf: create tree \"标题\" (4 entities)",
      "author": "PVForge AI",
      "time": "2026-11-09T12:34:56Z",
      "operation": "entity.createTree",
      "filesChanged": 4,
      "isUndo": false,
      "isRedo": false,
      "revertOf": null
    }
  ],
  "page": { "offset": 0, "limit": 20, "total": 1, "hasMore": false, "nextOffset": null }
}
```

**错误**：`not_available`（git 缺失）、`invalid_argument`（分页非法）。

**示例**：

```json
{ "limit": 20 }
```

---

## 11. 完整工作流

### 11.1 90 帧标题

目标：3 秒（90 帧 @ 30fps）标题动画，文字淡入上浮，下划线展开，结尾淡出。

**步骤 1：读概况**

调用 `project.summary`：

```json
{}
```

确认 `fps = 30`、`width = 1920`、`height = 1080`，当前内容为空。

**步骤 2：一次建树**

调用 `entity.createTree`：

```json
{
  "parentUuid": null,
  "root": {
    "name": "标题组",
    "start": 0,
    "end": 90,
    "row": 0,
    "content": { "kind": "empty" },
    "children": [
      {
        "name": "标题文字",
        "start": 0,
        "end": 90,
        "row": 1,
        "content": {
          "kind": "text",
          "text": {
            "text": "PVFORGE",
            "fontFamily": "Noto Sans SC",
            "pixelSize": 180,
            "color": "#ffffff",
            "bold": true,
            "align": 132
          }
        },
        "transform": { "x": 0, "y": -10, "opacity": 1 }
      },
      {
        "name": "下划线",
        "start": 0,
        "end": 90,
        "row": 0,
        "content": {
          "kind": "vector",
          "vector": {
            "shape": 0,
            "width": 900,
            "height": 14,
            "cornerRadius": 7,
            "fillColor": "#ffbe3c"
          }
        },
        "transform": { "x": 0, "y": 90, "scaleX": 0, "scaleY": 1 }
      }
    ]
  }
}
```

返回 `mapping` 里有三个 UUID：根 `"0"`、文字 `"0.0"`、下划线 `"0.1"`。记住这两个子实体 UUID。

**步骤 3：一次写多条轨道**

调用 `keyframes.setMany`：

```json
{
  "items": [
    {
      "uuid": "<标题文字>",
      "track": "opacity",
      "keys": [
        { "frame": 0, "value": 0, "easing": "linear" },
        { "frame": 12, "value": 1, "easing": "easeoutcubic" },
        { "frame": 75, "value": 1, "easing": "linear" },
        { "frame": 89, "value": 0, "easing": "easeincubic" }
      ]
    },
    {
      "uuid": "<标题文字>",
      "track": "y",
      "keys": [
        { "frame": 0, "value": 40, "easing": "easeoutcubic" },
        { "frame": 15, "value": -10, "easing": "linear" }
      ]
    },
    {
      "uuid": "<下划线>",
      "track": "scaleX",
      "keys": [
        { "frame": 0, "value": 0, "easing": "easeoutquart" },
        { "frame": 20, "value": 1, "easing": "linear" },
        { "frame": 75, "value": 1, "easing": "linear" },
        { "frame": 89, "value": 0, "easing": "easeincubic" }
      ]
    }
  ]
}
```

所有帧号都相对各自实体 `start`；这两个实体 `start = 0`，所以数值相同。若以后把实体整体移到 30 帧，关键帧**不需要**改。

**步骤 4：预览自查**

依次调用 `preview.render_frame`：

```json
{ "frame": 0 }
```

```json
{ "frame": 15 }
```

```json
{ "frame": 45 }
```

```json
{ "frame": 89 }
```

逐次调用，遵守 2 次/秒。检查：

- 第 0 帧：文字透明、下划线宽度 0；
- 第 15 帧：文字完全出现并回到 y=-10；
- 第 45 帧：文字和下划线都完整；
- 第 89 帧：两者淡出。

**步骤 5：微调**

如果文字入场太快：

调用 `keyframes.set`，只改这一条轨道：

```json
{
  "uuid": "<标题文字>",
  "track": "opacity",
  "keys": [
    { "frame": 0, "value": 0, "easing": "linear" },
    { "frame": 18, "value": 1, "easing": "easeoutcubic" },
    { "frame": 75, "value": 1, "easing": "linear" },
    { "frame": 89, "value": 0, "easing": "easeincubic" }
  ]
}
```

再预览第 18 帧确认。每次修改都是一个独立 commit，可以用 `history.undo` 回退。

---

### 11.2 重力下落的球

目标：球从左上抛下，受重力落到地面并反弹，最后停住。

**步骤 1：建场景**

调用 `entity.createTree`：

```json
{
  "parentUuid": null,
  "root": {
    "name": "重力演示",
    "start": 0,
    "end": 120,
    "row": 0,
    "content": { "kind": "empty" },
    "children": [
      {
        "name": "地面",
        "start": 0,
        "end": 120,
        "row": 0,
        "content": {
          "kind": "vector",
          "vector": {
            "shape": 0,
            "width": 1200,
            "height": 40,
            "cornerRadius": 0,
            "fillColor": "#334155"
          }
        },
        "transform": { "x": 0, "y": 360 },
        "physicsInteract": true
      },
      {
        "name": "小球",
        "start": 0,
        "end": 120,
        "row": 1,
        "content": {
          "kind": "vector",
          "vector": {
            "shape": 0,
            "width": 120,
            "height": 120,
            "cornerRadius": 60,
            "fillColor": "#ffbe3c",
            "strokeColor": "#ffffff",
            "strokeWidth": 4
          }
        },
        "transform": { "x": -360, "y": -360 }
      }
    ]
  }
}
```

地面 `physicsInteract: true` 才会参与碰撞；运动中的小球本身不需要打开这个开关。

**步骤 2：给小球写开始帧的值**

调用 `keyframes.setMany`：

```json
{
  "items": [
    {
      "uuid": "<小球>",
      "track": "x",
      "keys": [{ "frame": 0, "value": -360, "easing": "linear" }]
    },
    {
      "uuid": "<小球>",
      "track": "y",
      "keys": [{ "frame": 0, "value": -360, "easing": "linear" }]
    }
  ]
}
```

求解段要求开始帧在 `x`、`y` 上都有关键帧；这两个值就是初始位置。

**步骤 3：创建重力段**

调用 `solver.create`：

```json
{
  "entityUuid": "<小球>",
  "effect": "gravity",
  "startFrame": 0,
  "endFrame": 119,
  "initialVelocity": { "x": 540, "y": -240 },
  "params": {
    "targetUuid": "<地面>",
    "strength": 980,
    "elasticity": 0.45,
    "friction": 0.2,
    "stopThreshold": 3
  }
}
```

返回 `solver.id`，后续调整和采样都要用。

**步骤 4：采样检查**

调用 `solver.evaluate`：

```json
{
  "entityUuid": "<小球>",
  "solverId": "<solverId>",
  "frames": [0, 10, 20, 30, 40, 50, 57, 60, 80, 100, 119]
}
```

读 `samples`：

- `y` 应该先上升（初速度向上）再下降；
- `collided` 第一次为 `true` 的帧就是触地帧；
- 触地前 `velocity.y` 为正（向下）；反弹后 `velocity.y` 变为负值（向上）；
- 末尾 `stopped` 应为 `true`。

**步骤 5：按结果调参**

如果球弹得太高，调用 `solver.update`：

```json
{
  "entityUuid": "<小球>",
  "solverId": "<solverId>",
  "patch": {
    "params": { "strength": 1400, "elasticity": 0.55 }
  }
}
```

`params` 是浅合并，只改 `strength` 和 `elasticity`，其他参数保持。改完再 `solver.evaluate` 同一组帧。

**步骤 6：预览**

依次调用 `preview.render_frame`：

```json
{ "frame": 0 }
```

```json
{ "frame": 57 }
```

```json
{ "frame": 90 }
```

检查球和地面没有穿插、反弹高度合理、末尾停住。

**注意**：

- 重力方向是**目标地面实体**的局部 +Y；目标地面旋转时重力方向跟着转。运动小球自己的 rotation 不影响重力方向。
- 段内不要给 `x` / `y` 加普通关键帧；`keyframes.set` 会返回 `conflict`。
- `endFrame: 119` 是结束标记；到这一帧速度归零、值保持。想让球滚出画面，不要设结束标记，用 `-1` 或省略。

---

### 11.3 导入视频并拆音频

目标：导入一个视频，视频和原声都放到时间轴第 120 帧，素材从第 30 帧开始用。

**步骤 1：导入素材**

调用 `asset.import`：

```json
{
  "path": "/home/user/media/opening.mp4",
  "name": "片头",
  "extractAudio": true
}
```

返回：

```json
{
  "ok": true,
  "data": {
    "asset": {
      "hash": "<videoHash>",
      "type": "video",
      "name": "片头",
      "durationFrames": 240,
      "width": 1920,
      "height": 1080
    },
    "audioAsset": {
      "hash": "<audioHash>",
      "type": "audio",
      "name": "片头 音频",
      "durationFrames": 240,
      "sampleRate": 48000,
      "channels": 2
    },
    "deduplicated": false,
    "extractedAudio": true
  },
  "commit": { "hash": "...", "shortHash": "...", "message": "pvf: import asset \"片头\"" }
}
```

**步骤 2：一次创建视频和音频实体**

调用 `entity.createMany`：

```json
{
  "entities": [
    {
      "name": "片头视频",
      "start": 120,
      "end": 330,
      "row": 0,
      "content": {
        "kind": "asset",
        "asset": {
          "assetHash": "<videoHash>",
          "assetType": "video",
          "sourceIn": 30,
          "volume": 1.0
        }
      }
    },
    {
      "name": "片头原声",
      "start": 120,
      "end": 330,
      "row": 1,
      "content": {
        "kind": "asset",
        "asset": {
          "assetHash": "<audioHash>",
          "assetType": "audio",
          "sourceIn": 30,
          "volume": 1.0
        }
      }
    }
  ]
}
```

计算：素材 240 帧，`sourceIn = 30`，所以最多用 210 帧；`end = 120 + 210 = 330`。两个实体都从全局帧 120 开始，`sourceIn = 30` 表示实体第 0 帧对应素材第 30 帧。

**步骤 3：预览**

依次调用 `preview.render_frame`：

```json
{ "frame": 120 }
```

```json
{ "frame": 200 }
```

```json
{ "frame": 329 }
```

检查画面、黑边、音频是否存在（音频不能靠预览听，靠 `view.state` 和实体数据确认）。

**步骤 4：换素材（可选）**

调用 `asset.assign`：

```json
{
  "entityUuid": "<片头视频>",
  "assetHash": "<newVideoHash>",
  "sourceIn": 30
}
```

如果新素材比当前短，返回的 `adjusted` 会给出裁短后的 `end` 和被删除的关键帧数量。

**注意**：

- 视频和音频在导入后**完全独立**；移动/裁剪视频不会联动音频。要维持同步，必须对两个实体做同样的 `entity.move` / `entity.trim`。
- 裁剪视频 start 右移 `d` 时，`sourceIn += d`；音频也要同样处理。
- 删除视频实体不会删除音频实体，反之亦然。
- 素材缺失时实体仍保留，重新导入相同内容即可恢复。

---

## 12. 写操作检查清单

AI 在提交写操作前按顺序自查：

1. 读 `project.summary`，确认 fps、尺寸、当前内容长度。
2. 读 `entity.list` / `entity.get`，确认目标 UUID、父级、行、已有轨道，避免覆盖人类工作。
3. 确认目标实体和祖先没有锁定；锁定就停下来报告人类。
4. 关键帧帧号是相对实体 `start` 的整数，且都在 `0..duration-1`。
5. 求解段开始帧已有 `x`/`y` 或 `rotation` 关键帧；段与段不重叠；段内没有普通关键帧。
6. 危险操作（删除、删素材、级联裁剪）带 `confirm: true`。
7. 批量操作的元素数量不超过 200；工程实体不超过 5000。
8. 写完用 `preview.render_frame` 抽查起始、中间、结束帧；失败就停，不叠加修改。
9. 收到 `stopped_by_human` 立即停止，绝不重试。
10. 需要回退时用 `history.undo`，不要用“再写一次相反的值”模拟撤销。

---

## 13. 尚未定稿的 schema

以下字段/枚举在 `ENTITY_MODEL.md` 和 `IMPLEMENTATION_CONTRACT.md` 里还没有完整定义。本文按现有信息写了最小可用契约；实现落地时应补进 `schema.get`，UI tooltip 也以 `schema.get` 为准。

| 缺口 | 影响 | 建议 |
|---|---|---|
| `vector.shape` 枚举值 | AI 不知道 0/1/2/3 分别是什么形状 | 在 `schema.get("entity")` 里给出 `shape` 的 enum 和中文名 |
| `text.align` 位掩码完整含义 | 只知道 132 = 居中 | 给出 Qt 对齐位表或常用组合 |
| `easing` 预设全集 | 本文按旧 `Easing` 枚举列出 33 个名字 | 新模型 `EasingSpec.type` 定稿后同步 |
| HTML `var.<name>` 轨道类型 | 目前只约定数字轨道 | 从 `.pvhtml` manifest 的 `variables` 生成 |
| `solver.list` / `evaluate` 的 `computed` 字段 | 目前只定义了 orbit 的 `semiMajor`/`semiMinor` | 补全所有效果的只读派生字段 |
| 素材导入大小上限 | 本文只说“服务端上限” | 写进 `schema.get("asset")` 或设置项 |
| 预览超时/输出大小上限 | 本文给 4096 边长和 2 次/秒 | 按实现补 `render_timeout` 阈值和 PNG 大小上限 |
| `history.log` 的 `operation` 取值 | 目前用工具名 | 定稿后与 commit 消息前缀对齐 |
| 错误 `details` 的稳定键 | 本文列了常用键 | 各工具实现时补全并写进 `schema.get("errors")` |
