# AI 文档（非用户文档）

# PVForge MCP 动画制作指南（给 AI 代理）

> 目标读者：通过 MCP 操作 PVForge 的另一个 AI。  
> 事实来源：当前代码（PR1 基线 `05c0324` + 本 PR 的 MCP 行为修正）。  
> 与 `docs/human/` 冲突时以人类规范为准；本文档只描述如何用 MCP 做动画。

---

## 0. 先给 AI 的系统提示词（可直接复制）

```text
你是 PVForge 的动画代理。你通过本机 MCP HTTP 服务操作一个已经在 GUI 中打开的 PVForge 工程。

硬规则：
1. 只通过 MCP 工具读写工程；不要直接编辑 .pvproj 目录里的 JSON，也不要在 PVForge 打开工程时改磁盘文件。
2. 每次写之前先读（project.summary / entity.list / entity.get / keyframes.get / view.state）。
3. 一次工具调用只做一个逻辑操作；批量用 createMany / updateMany / setMany，失败会整批回滚。
4. 失败先读 error.code / details 修正参数；permission_denied、confirm_required、stopped_by_human 不要盲目重试。
5. 实体时间是全局帧闭区间 [start,end]，end 帧也可见；时长 = end-start+1。
6. 关键帧是“整帧”：frame 相对实体 start，范围 0..end-start；每个关键帧必须包含全部动画参数。
7. 画面重叠的实体放在不同 row；同一父级同一 row 的闭区间不能重叠。
8. 预览限流约 2 次/秒；渲染后用返回的 path 读取 PNG，不要自己猜路径。
9. 最后必须用 entity.get / keyframes.get / preview.render_frame 自查，再向人类报告。
10. 你没有导出工具；最终导出由人类在 PVForge GUI 里完成。
```

---

## 1. 连接 PVForge MCP

### 1.1 前提

- 人类必须在 PVForge GUI 中新建/打开一个 `.pvproj` 工程，或启动 `pvforge --open /path/to/foo.pvproj`。
- MCP 服务随工程会话自动启动（每个打开工程一个监听）。
- **写操作要求工程启用 Git**（设置里的版本管理开关）。未启用时所有写工具返回 `permission_denied`，`details.reason = git_disabled`；只读工具仍可用。

### 1.2 找到端点

默认端点文件：

| 平台 | 路径 |
|---|---|
| Linux | `${XDG_CONFIG_HOME:-~/.config}/PVForge/mcp-endpoint.json` |
| macOS | `~/Library/Application Support/PVForge/mcp-endpoint.json` |

文件样例（含敏感 token，不要外传）：

```json
{
  "format": "pvforge-mcp-endpoint",
  "version": 1,
  "url": "http://127.0.0.1:47821/mcp",
  "port": 47821,
  "token": "<64 位十六进制 Bearer token>",
  "pid": 12345,
  "nonce": "..."
}
```

若端口被占用，PVForge 会自动换端口，**以文件里的 url 为准**。  
应用每次启动都会轮换 token；切换工程会重建监听。**重启/切换工程后必须重新读 endpoint 文件**，不要缓存旧 token/url。

### 1.3 协议

- `POST <url>`，只接受 `Content-Type: application/json`。
- 必须带头：`Authorization: Bearer <token>`。
- JSON-RPC 2.0；不支持 GET / SSE / chunked。
- 请求体 ≤ 1 MiB；单次工具调用内部超时 15 秒，客户端建议 20 秒。
- `initialize` 返回 `Mcp-Session-Id`，但服务端不校验，后续请求可以不回传。
- 工具调用是串行的：上一个没结束时再发会被拒绝（`Server busy`）。
- JSON-RPC 层错误也是 HTTP 200 + `error` 对象；HTTP 401 才是 token 错。

最小连接序列：

```json
{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"my-ai","version":"1.0"}}}
{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}
{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"project.summary","arguments":{}}}
```

工具结果在：

```json
{
  "jsonrpc": "2.0",
  "id": 3,
  "result": {
    "content": [{"type": "text", "text": "{\"ok\":true,...}"}],
    "structuredContent": {"ok": true, "data": { ... }, "commit": {"hash": "..."}},
    "isError": false
  }
}
```

**优先解析 `result.structuredContent`**；`content[0].text` 是同一 JSON 的字符串。成功：

```json
{"ok": true, "data": {...}, "commit": {"hash": "...", "shortHash": "..."}}
```

失败：

```json
{"ok": false, "error": {"code": "invalid_argument", "message": "...", "details": {...}}}
```

### 1.4 Python 标准库客户端（可直接用）

```python
import json, time, urllib.request

ENDPOINT = "/home/you/.config/PVForge/mcp-endpoint.json"

class PvForge:
    def __init__(self, endpoint=ENDPOINT):
        ep = json.load(open(endpoint, encoding="utf-8"))
        self.url = ep["url"]
        self.token = ep["token"]
        self.next_id = 1

    def rpc(self, method, params=None):
        body = json.dumps({
            "jsonrpc": "2.0", "id": self.next_id,
            "method": method, "params": params or {},
        }).encode()
        self.next_id += 1
        req = urllib.request.Request(self.url, data=body, headers={
            "Authorization": "Bearer " + self.token,
            "Content-Type": "application/json",
        })
        with urllib.request.urlopen(req, timeout=20) as response:
            return json.loads(response.read())

    def call(self, tool, args=None):
        env = self.rpc("tools/call", {"name": tool, "arguments": args or {}})
        if "error" in env:                 # JSON-RPC 层错误
            raise RuntimeError(env["error"])
        return env["result"]["structuredContent"]   # {ok, data/error, commit?}

    def must(self, tool, args=None):
        result = self.call(tool, args)
        if not result.get("ok"):
            raise RuntimeError(json.dumps(result["error"], ensure_ascii=False))
        return result

    def preview(self, frame, width=None, height=None, camera_uuid=None):
        args = {"frame": frame}
        if width: args["width"] = width
        if height: args["height"] = height
        if camera_uuid: args["cameraUuid"] = camera_uuid
        data = self.must("preview.render_frame", args)["data"]
        time.sleep(0.55)                    # 服务端约 2 次/秒
        return data["path"]                 # 读这个 PNG
```

---

## 2. 必须理解的模型

### 2.1 坐标与单位

- 项目画布**中心是原点**，X 向右，Y 向上。
- 640×360 工程可见范围约 `x ∈ [-320, 320]`、`y ∈ [-180, 180]`。
- `transform.x/y`、矢量 `width/height`、图片/视频/HTML 的局部尺寸单位都是画布像素。
- `rotation` 单位是度；正角度在画面上逆时针。
- `scaleX/scaleY` 为负会翻转。
- 文本、矢量、图片、HTML 都以实体原点为中心绘制。

### 2.2 闭区间

- 实体时间：全局帧闭区间 `[start, end]`，`end` 帧也可见。
- `duration = end - start + 1`；单帧实体 `[10,10]` 合法。
- 关键帧 `frame` 是**相对实体 start**，合法范围 `0 .. duration-1`。
- 例：实体 `[100, 219]` 时长 120 帧，关键帧帧号只能是 `0..119`；绝对第 130 帧对应关键帧 `30`。
- 时间轴右边缘在 `end + 1` 处，所以相邻片段 `[0,133]` 与 `[134,266]` 无缝。

### 2.3 行（row）与父子

- 同一父级、同一 `row` 的实体闭区间**不能重叠**。要在同一时间叠多层，请用不同 row。
- `row` 越大越靠上（后绘制）。根时间轴 row ≥ 0；私有时间线允许负 row（负 row 画在父实体后面，`entity.update`/`entity.move`/`entity.create`/`entity.reparent` 都遵守这条；MCP 可写范围 `-1000000..1000000`）。
- `entity.reparent` 到主时间线（`parentUuid=null`）时不能留下负 row：显式负 row 会被拒绝，不给 row 时自动分配非负行号。
- 子实体范围必须落在父实体范围内。
- `parentUuid` 为空 = 根实体；`space`：
  - `parent`（默认）：继承父级 transform；
  - `canvas`：位置/旋转/缩放不继承父级，但父子关系/时间线仍存在。
- `followParentRange=true`：子实体区间必须始终等于直接父区间，单独 move/trim 会被拒绝。

### 2.4 实体内容：定义参数 vs 动画参数

`content` 只放**定义参数**（文字内容、字体、颜色、形状类型、素材引用等）。  
动画参数放在每个整帧关键帧的 `content` 里。

**嵌套规则**：`text`/`vector` 的定义参数必须放在嵌套子对象里（`content.text` / `content.vector`）；`asset`/`camera`/`empty` 的字段放在 `content` 顶层（`assetHash`/`assetType`/`sourceIn`/`volume`/`fadeIn`/`fadeOut`）。`entity.create` 与 `entity.update` 都遵守这个规则；`entity.update` 传扁平字段（例如 `{"kind":"vector","shape":5}`）会返回 `invalid_argument`，`details.expectedContentPath` 给出正确嵌套路径，`details.unexpectedKeys` 列出被拒的顶层键。

| content.kind | 定义参数（静态） | 关键帧里的动画参数（必须全给） |
|---|---|---|
| `text` | `text`、`fontFamily`、`color`、`strokeColor`、`align`、`bold`、`italic`、`underline`、`vertical`、`verticalRtl` | `pixelSize`、`lineHeight`、`letterSpacing`、`padding`、`boxWidth`、`boxHeight`、`strokeWidth` |
| `vector` | `shape`、`sides`、`innerRatio`、`fillColor`、`strokeColor` | `width`、`height`、`cornerRadius`、`strokeWidth` |
| `asset` image/video | `assetHash`、`assetType`、`sourceIn`、`volume`、`fadeIn`、`fadeOut` | 无（用整帧的 transform 做动画） |
| `asset` audio | `assetHash`、`assetType`、`sourceIn`、`volume`、`fadeIn`、`fadeOut` | `volume`（线性增益） |
| `asset` html | `assetHash`、`assetType`、`sourceIn`、`volume`、`fadeIn`、`fadeOut` | 无；数字变量放 `htmlVars` |
| `camera` | `{}`（`content.camera` 只接受空对象） | 无（用整帧 transform 做运镜） |
| `empty` | `{}` | 无（可做父级/重力目标） |

`volume`/`fadeIn`/`fadeOut` 是 asset 通用字段：任何 `assetType` 都能写，不会被拒（它们只影响音频混音；`volume` 是线性增益，`fadeIn`/`fadeOut` 单位是秒、按实体区间裁剪）。

**矢量 `shape` 是整数，不是字符串**（字符串会被忽略并退回 0=矩形）：

| 值 | 形状 |
|---|---|
| 0 | rect 矩形 |
| 1 | ellipse 椭圆 |
| 2 | line 线 |
| 3 | rounded rect 圆角矩形 |
| 4 | triangle 三角形 |
| 5 | heart 心形 |
| 6 | star 星形 |
| 7 | polygon 多边形 |
| 8 | arrow 箭头 |

`text.align` 是 Qt 对齐位标志，默认 132（水平+垂直居中）。  
文本/矢量里的 `width/height/pixelSize` 等即使可动画，也建议在静态 `content` 里给一个基准值；关键帧必须再给全。

### 2.5 整帧关键帧

每个关键帧对象：

```json
{
  "frame": 0,
  "x": 0, "y": 0, "rotation": 0,
  "scaleX": 1, "scaleY": 1, "opacity": 1,
  "visible": true, "physicsInteract": false,
  "content": { ...该内容类型的全部动画参数... },
  "htmlVars": { "pulse": 0.0 },
  "easing": "easeoutcubic",
  "note": "标题入场"
}
```

规则：

- `frame/x/y/rotation/scaleX/scaleY/opacity/visible/physicsInteract` 必填；`opacity` 必须在 `0..1`。
- `content` 必须包含该内容类型的**全部**动画参数；`image/video/html/camera/empty` 可省略或给 `{}`。
- `htmlVars` 只接受有限数值；非数字变量不能进关键帧。
- `easing` 是**左关键帧到下一关键帧**的缓动；最后一个关键帧的 easing 不使用。
- `visible` / `physicsInteract` 是步骤值：取左侧关键帧，不做插值。
- 第一个关键帧之前、最后一个关键帧之后保持首/尾值。
- 关键帧按 frame 排序，重复帧会被拒绝。

完整缓动白名单（**大小写不敏感**；接受后统一归一化为模型规范名，序列化/回读都是下列小写名）：

```text
linear, instant, bezier,
easeinquad/easeoutquad/easeinoutquad,
easeincubic/easeoutcubic/easeinoutcubic,
easeinquart/easeoutquart/easeinoutquart,
easeinquint/easeoutquint/easeinoutquint,
easeinsine/easeoutsine/easeinoutsine,
easeinexpo/easeoutexpo/easeinoutexpo,
easeincirc/easeoutcirc/easeinoutcirc,
easeinelastic/easeoutelastic/easeinoutelastic,
easeinback/easeoutback/easeinoutback,
easeinbounce/easeoutbounce/easeinoutbounce
```

兼容别名（同样大小写不敏感，归一化后写入规范名）：`easein` → `easeinquad`、`easeout` → `easeoutquad`、`easeinout` → `easeinoutquad`、`step` → `instant`。  
也就是说 `EaseOutCubic`、`easeoutcubic`、`easeInOut`、`easeinout`、`step`、`Step` 都能用；未知名字仍然返回 `invalid_argument`。对象写法 `{type:"bezier",...}` 与字符串 `"bezier"` 都走贝塞尔分支。

自定义贝塞尔：

```json
"easing": {"type": "bezier", "inX": 0.42, "inY": 0.0, "outX": 0.58, "outY": 1.0}
```

### 2.6 求解段（物理）

- 求解段 `startFrame/endFrame` 也是**相对实体 start**。
- `endFrame = -1`（默认）：自动解到第一次物理停止/碰撞，或片段末帧；如果起点之后出现新的普通整帧关键帧 N，则自动段在 N-1 结束。
- `endFrame >= 0`：显式结束；`startFrame+1 .. endFrame`（含）内不能有普通整帧关键帧。
- **起点必须有一个普通整帧关键帧**。
- 效果参数（工具允许的字段）：

| effect | 必填 | 常用参数（单位） |
|---|---|---|
| `gravity` | `params.targetUuid` | `strength` px/s²（默认 980）、`elasticity` 0..1、`friction` 0..1、`stopThreshold` px/s |
| `orbit` | `targetUuid` | `aspect` 0.01..1、`angle` 度、`direction` ±1、`periodFrames` 帧/圈 |
| `spin` | 无目标 | `angularVelocity` **度/帧**（默认 6）、`direction` ±1 |
| `blackhole` | `targetUuid` | `simulationSpeed`、`mass`、`angularMomentum`、`eventHorizonRadius`、`objectRadius`、`softening`、`maxSpeed`、`captureShrink` |
| `spiral` | `targetUuid` | `turns`、`aspect`、`angle`、`direction`（+1 逆时针）、`speedDegPerSec` |

- `initialVelocity` 是 `{x,y}` px/s，只有 gravity/blackhole 使用。
- 求解目标不能是自己，也不能形成循环依赖。
- `solver.evaluate` 只读，返回相对实体 start 的采样点、速度、`stopped/collided` 和 `summary`。

### 2.7 渲染顺序

- 同一父级按 row 从小到大绘制；row 越大越靠上。
- 私有时间线：负 row 子实体先于父实体绘制，非负 row 在父实体之后。
- `hidden` 只是**工作台/编辑器画布**的可见性（`skipHidden` 时隐藏，不影响导出、摄像机监视器和 MCP `preview.render_frame`）；要控制最终画面请用 `visible`（可以关键帧化）。
- `inheritVisibility=true` 时父级 `visible` 会级联隐藏子级。

---

## 3. 工具速查

完整输入 schema 以 `tools/list` 或 `schema.get` 为准。下表是动画制作最常用的字段。

### 3.1 工程 / 视图

| 工具 | 关键参数 | 说明 |
|---|---|---|
| `project.get` | 无 | 设置、路径、计数、git、`write.enabled/stopped` |
| `project.summary` | 无 | 轻量概况，`durationFrames` = 最晚 `end+1` |
| `project.update` | `name`、`background` | `fps/width/height` 创建后锁定，传了会 `conflict` |
| `view.state` | 无 | 播放头、选中 UUID、私有时间线、活动摄像机、视口范围 |
| `preview.render_frame` | `frame`、`cameraUuid`、`width`、`height`、`background`、`includeBase64` | 返回临时 PNG 的 `path`；**没有 `path` 入参**；约 2 次/秒 |

### 3.2 实体

| 工具 | 关键参数 | 说明 |
|---|---|---|
| `entity.create` | `start` 必填；`end`、`row`、`parentUuid`、`space`、`content`、`transform`、`keyframes`、`solvers` | 非 timed 默认时长 1 秒（`end=start+fps-1`）；音频/视频默认取素材剩余长度；row 默认最高占用 +1（根时间线结果非负，私有时间线可为负）；`row` 可写 `-1000000..1000000`，根实体必须 ≥ 0 |
| `entity.createMany` | `entities[]` | 1..200 个，整批一个 commit；每个可带自己的 `parentUuid`（必须是调用前已存在） |
| `entity.createTree` | `parentUuid`、`root` | 递归 `children`；子节点省略 start/end 时继承父节点区间；最多 200 节点/深度限制 |
| `entity.get` | `uuid`、`frame`、`includeChildren` | 返回完整实体、关键帧、求解段、指定帧 `evaluated` |
| `entity.list` | `parentUuid`、`offset`、`limit`、`includeKeyframes`、`includeSolvers` | 分页；`parentUuid` 省略 = 全部 |
| `entity.update` | `uuid`、`patch` | 局部改 `name/row/hidden/locked/visible/inheritVisibility/physicsInteract/space/followParentRange/transform/content`；不改时间/父级/关键帧/素材引用。`patch.content` 必须按 §2.4 嵌套写（`content.text`/`content.vector`）；扁平字段返回 `invalid_argument` + `details.expectedContentPath`/`unexpectedKeys`。`row` 规则同 create |
| `entity.updateMany` | `items[{uuid,patch}]` | 1..200，整批一个 commit |
| `entity.move` | `uuid`、`start` 或 `delta`、`row` | 移动子树；`start`/`delta` 二选一 |
| `entity.trim` | `uuid`、`start`、`end` | 至少给一个；级联裁剪子树；删子实体需要 `confirm` |
| `entity.reparent` | `uuid`、`parentUuid`、`mode` | `preserveWorld`（有整帧关键帧会拒绝）或 `keepLocal`；求解依赖需 `confirm` |
| `entity.duplicate` | `uuid`、`deep`(默认 true)、`offsetFrames`、`parentUuid`、`name`、`row` | 生成新 UUID |
| `entity.delete` | `uuid`、`confirm=true` | 删除整棵子树 |
| `entity.deleteMany` | `uuids[]`、`confirm=true` | 批量删除 |

### 3.3 关键帧

| 工具 | 关键参数 | 说明 |
|---|---|---|
| `keyframes.get` | `uuid` | 返回 `duration`、`count`、`keyframes[]` |
| `keyframes.set` | `uuid`、`keyframes[]` | 整组替换；缺多个动画参数时一次返回 `invalid_argument`，`details.missingFields` 列全部缺失字段、`details.requiredFields` 列该内容类型全部必填字段 |
| `keyframes.setMany` | `items[{uuid,keyframes}]` | 多实体整组替换，一个 commit；每项只能有 `uuid` 和 `keyframes` 两个键，同批 uuid 不能重复 |
| `keyframes.upsert` | `uuid`、`frame`、`partial` | 先按该帧求值补全全部动画参数，再覆盖 `partial`；求解段内会取当前求解姿态 |
| `keyframes.remove` | `uuid`、`frames[]` | 幂等；求解起点不能删 |
| `keyframes.move` | `uuid`、`fromFrame`、`toFrame`、`overwrite` | 移动一个整帧 |
| `keyframes.clear` | `uuid` | 清空 |
| `keyframes.setEasing` | `uuid`、`frame`、`easing` | 只改已有关键帧 |

### 3.4 素材

| 工具 | 关键参数 | 说明 |
|---|---|---|
| `asset.import` | `path` 绝对路径；`name`、`type`、`extractAudio` | 工程内路径直接导入；工程外要人类在 GUI 确认；视频默认拆 WAV，返回 `asset` + `audioAsset`（两者都是**完整素材对象**，hash 取 `audioAsset.hash`，不是字符串）；同内容 sha256 去重 |
| `asset.list` | `type`、`query`、`offset`、`limit` | 素材没有 UUID，只有 `hash` |
| `asset.get` | `hash` | 含 `usedBy` 引用列表 |
| `asset.assign` | `entityUuid`、`assetHash`、`sourceIn`、`volume` | 只用于 empty/asset；视觉↔视觉、音频↔音频兼容；视频/音频会按可用长度裁短实体 |
| `asset.unassign` | `entityUuid` | 内容变成 empty |
| `asset.rename` / `asset.setTags` / `asset.delete` | `hash` 等 | `delete` 必须 `confirm=true` |

### 3.5 标记 / 历史

| 工具 | 关键参数 | 说明 |
|---|---|---|
| `markers.set` | `frame`、`title`、`tags`（最多 32 个）、`color`（CSS hex）；`id` 省略=新建 | 标记不渲染，给人类/AI 标节拍 |
| `markers.list` | `fromFrame`、`toFrame`、分页 | |
| `markers.remove` | `id` | |
| `history.undo` / `history.redo` | `steps` 1..10 | 内存撤销/重做，不产生新 git commit |
| `history.log` | `offset`、`limit` | git 冷备历史 |

### 3.6 自描述

| 工具 | 说明 |
|---|---|
| `tools/list` | 所有工具 + JSON Schema |
| `schema.get` | `topic=project/entity/keyframes/asset/markers/errors/solver/preview/view/all` |
| `guide.get` | 内置 Markdown：`authoring/workflow/solver/preview/assets/examples` |

---

## 4. 标准动画工作流

### 第 1 步：读状态

```python
pv = PvForge()
print(pv.must("project.summary")["data"])
print(pv.must("entity.list", {"limit": 200})["data"])
print(pv.must("view.state")["data"])
```

检查：

- `fps`、`width`、`height`、`durationFrames`
- `writeEnabled` / `stopped`（`project.get` 的 `write`）
- 现有实体 UUID、row、区间，避免同轨重叠
- `activeCameraUuid`（为空 = 全画布）

### 第 2 步：把秒换算成帧

```python
fps = summary["fps"]
start = round(seconds_start * fps)
end = start + round(duration_seconds * fps) - 1
```

4 秒 @30fps = 120 帧 = `[0, 119]`。  
关键帧相对帧 = 绝对帧 - `entity.start`。

### 第 3 步：建场景

- 背景/底板：vector rect，row 0。
- 装饰：vector，row 1..n。
- 文字/图片/HTML：更高 row。
- 需要运镜就建 `camera` 实体。
- 需要物理就建目标实体（empty 或可见形状）。

### 第 4 步：写整帧关键帧

- 一次性给多个实体用 `keyframes.setMany`（一个 commit，失败全回滚）。
- 只改一个已有关键帧的少量字段用 `keyframes.upsert`。
- 不要用「逐字段 try/retry」把一次动画拆成很多 commit。

### 第 5 步：预览

```python
png = pv.preview(60, width=640, height=360)
```

- 读关键时间点（0 / 入场 / 高潮 / 出场 / end）。
- `width`/`height` 只给一边时按工程比例补另一边。
- 返回的 `path` 在 `/tmp/pvforge-previews-<uid>/`，1 小时/32 张后轮转。
- 约 2 次/秒，连续调用会 `limit_exceeded`；客户端 sleep 0.55s。
- `preview.render_frame` **不接受 `path` 参数**，不要按旧文档传。

### 第 6 步：标注节拍

用 `markers.set` 标 `0/30/60/90/119`，用关键帧 `note` 写「副歌鼓点」「标题入场」。人类和后续 AI 都靠这些定位。

### 第 7 步：自查并交给人类

- `entity.get(uuid, frame=关键帧)` 看 `evaluated`。
- `keyframes.get` 看排序/数量/字段。
- `preview.render_frame` 看构图。
- 告诉人类：工程已保存为 git commits；最终导出请用 GUI。

---

## 5. 完整示例：4 秒标题动画

工程：640×360、30fps、背景 `#101820`、总长 120 帧 `[0,119]`。

### 5.1 建实体

```json
{
  "entities": [
    {
      "name": "Backdrop", "start": 0, "end": 119, "row": 0,
      "content": {"kind": "vector", "vector": {
        "shape": 0, "width": 640, "height": 360, "cornerRadius": 0,
        "strokeWidth": 0, "fillColor": "#16213e", "strokeColor": "#00000000"
      }}
    },
    {
      "name": "Glow", "start": 0, "end": 119, "row": 1,
      "content": {"kind": "vector", "vector": {
        "shape": 1, "width": 260, "height": 260, "cornerRadius": 0,
        "strokeWidth": 0, "fillColor": "#0f3460", "strokeColor": "#00000000"
      }}
    },
    {
      "name": "Title", "start": 0, "end": 119, "row": 2,
      "content": {"kind": "text", "text": {
        "text": "PVFORGE", "pixelSize": 84, "color": "#f2c94c",
        "align": 132, "lineHeight": 1.0, "letterSpacing": 0,
        "padding": 0, "boxWidth": 0, "boxHeight": 0, "strokeWidth": 0,
        "bold": true
      }}
    },
    {
      "name": "Subtitle", "start": 0, "end": 119, "row": 3,
      "content": {"kind": "text", "text": {
        "text": "MCP · ANIMATION", "pixelSize": 26, "color": "#d9e2ec",
        "align": 132, "lineHeight": 1.0, "letterSpacing": 2,
        "padding": 0, "boxWidth": 0, "boxHeight": 0, "strokeWidth": 0
      }}
    },
    {
      "name": "Camera", "start": 0, "end": 119, "row": 4,
      "content": {"kind": "camera", "camera": {}}
    }
  ]
}
```

工具：`entity.createMany`。返回 `data.created[].uuid`，记下 Title/Glow/Subtitle/Camera 的 UUID。

### 5.2 写关键帧

```json
{
  "items": [
    {
      "uuid": "<GLOW_UUID>",
      "keyframes": [
        {"frame": 0, "x": -150, "y": 40, "rotation": 0, "scaleX": 1, "scaleY": 1,
         "opacity": 1, "visible": true, "physicsInteract": false,
         "content": {"width": 260, "height": 260, "cornerRadius": 0, "strokeWidth": 0},
         "easing": "easeinoutsine"},
        {"frame": 60, "x": 0, "y": 40, "rotation": 0, "scaleX": 1, "scaleY": 1,
         "opacity": 1, "visible": true, "physicsInteract": false,
         "content": {"width": 320, "height": 320, "cornerRadius": 0, "strokeWidth": 0},
         "easing": "easeinoutsine"},
        {"frame": 119, "x": 150, "y": 40, "rotation": 0, "scaleX": 1, "scaleY": 1,
         "opacity": 1, "visible": true, "physicsInteract": false,
         "content": {"width": 260, "height": 260, "cornerRadius": 0, "strokeWidth": 0}}
      ]
    },
    {
      "uuid": "<TITLE_UUID>",
      "keyframes": [
        {"frame": 0, "x": -280, "y": 20, "rotation": 0, "scaleX": 0.8, "scaleY": 0.8,
         "opacity": 0, "visible": true, "physicsInteract": false,
         "content": {"pixelSize": 84, "lineHeight": 1.0, "letterSpacing": 0,
                     "padding": 0, "boxWidth": 0, "boxHeight": 0, "strokeWidth": 0},
         "easing": "easeoutcubic", "note": "标题从左入场"},
        {"frame": 30, "x": 0, "y": 20, "rotation": 0, "scaleX": 1, "scaleY": 1,
         "opacity": 1, "visible": true, "physicsInteract": false,
         "content": {"pixelSize": 84, "lineHeight": 1.0, "letterSpacing": 0,
                     "padding": 0, "boxWidth": 0, "boxHeight": 0, "strokeWidth": 0},
         "easing": "easeinoutquad"},
        {"frame": 90, "x": 0, "y": 20, "rotation": 0, "scaleX": 1, "scaleY": 1,
         "opacity": 1, "visible": true, "physicsInteract": false,
         "content": {"pixelSize": 84, "lineHeight": 1.0, "letterSpacing": 0,
                     "padding": 0, "boxWidth": 0, "boxHeight": 0, "strokeWidth": 0},
         "easing": "easeincubic"},
        {"frame": 119, "x": 280, "y": 20, "rotation": 0, "scaleX": 1.1, "scaleY": 1.1,
         "opacity": 0, "visible": true, "physicsInteract": false,
         "content": {"pixelSize": 84, "lineHeight": 1.0, "letterSpacing": 0,
                     "padding": 0, "boxWidth": 0, "boxHeight": 0, "strokeWidth": 0},
         "note": "标题向右出场"}
      ]
    },
    {
      "uuid": "<SUBTITLE_UUID>",
      "keyframes": [
        {"frame": 15, "x": 0, "y": -70, "rotation": 0, "scaleX": 1, "scaleY": 1,
         "opacity": 0, "visible": true, "physicsInteract": false,
         "content": {"pixelSize": 26, "lineHeight": 1.0, "letterSpacing": 2,
                     "padding": 0, "boxWidth": 0, "boxHeight": 0, "strokeWidth": 0},
         "easing": "easeoutsine"},
        {"frame": 35, "x": 0, "y": -70, "rotation": 0, "scaleX": 1, "scaleY": 1,
         "opacity": 1, "visible": true, "physicsInteract": false,
         "content": {"pixelSize": 26, "lineHeight": 1.0, "letterSpacing": 2,
                     "padding": 0, "boxWidth": 0, "boxHeight": 0, "strokeWidth": 0},
         "easing": "linear"},
        {"frame": 95, "x": 0, "y": -70, "rotation": 0, "scaleX": 1, "scaleY": 1,
         "opacity": 1, "visible": true, "physicsInteract": false,
         "content": {"pixelSize": 26, "lineHeight": 1.0, "letterSpacing": 2,
                     "padding": 0, "boxWidth": 0, "boxHeight": 0, "strokeWidth": 0},
         "easing": "easeinsine"},
        {"frame": 119, "x": 0, "y": -70, "rotation": 0, "scaleX": 1, "scaleY": 1,
         "opacity": 0, "visible": true, "physicsInteract": false,
         "content": {"pixelSize": 26, "lineHeight": 1.0, "letterSpacing": 2,
                     "padding": 0, "boxWidth": 0, "boxHeight": 0, "strokeWidth": 0}}
      ]
    }
  ]
}
```

工具：`keyframes.setMany`。

注意：`Backdrop` 与 `Camera` 没有关键帧，静态 transform 就够。  
所有文本关键帧的 `content` 都完整包含 7 个动画参数——不能只给 `pixelSize`。

### 5.3 标记与预览

```python
for frame, title in [(0, "开场"), (30, "标题入场"), (60, "中段"),
                     (90, "标题停留"), (119, "结尾")]:
    pv.must("markers.set", {"frame": frame, "title": title,
                            "tags": ["demo"], "color": "#f2c94c"})

for frame in [0, 15, 30, 60, 90, 119]:
    path = pv.preview(frame, width=640, height=360)
    print(frame, path)          # 用 read/image 工具查看这张 PNG
```

---

## 6. 媒体、HTML、摄像机、求解

### 6.1 图片

1. `asset.import`（工程内路径无需人类确认）。
2. `entity.create`：
   ```json
   {"name":"Logo","start":0,"end":119,"row":5,
    "content":{"kind":"asset","assetHash":"<hash>","assetType":"image"},
    "transform":{"x":0,"y":0,"scaleX":0.5,"scaleY":0.5}}
   ```
3. 动画用整帧 transform（x/y/rotation/scaleX/scaleY/opacity）；关键帧 `content` 可给 `{}`。
4. 图片原始尺寸由素材元数据决定；要精确尺寸就按 `asset.get.width/height` 计算 scale。

### 6.2 视频

- 视频实体只负责画面；导入时默认拆出的 WAV 是独立音频素材（`asset.import` 返回 `audioAsset`）。
- `sourceIn` 是素材内起始帧；实体时长不能超过 `durationFrames - sourceIn`。
- 要视频带声音：再建一个 audio 实体，使用 `audioAsset.hash`，区间与视频对齐，用 `volume` 关键帧控制。
- 不同源帧号会正确失效工作台缓存；不要指望缓存把视频当静止图。

### 6.3 音频

```json
{"name":"Beat","start":0,"end":89,"row":6,
 "content":{"kind":"asset","assetHash":"<wav hash>","assetType":"audio",
            "volume":1.0,"fadeIn":0.2,"fadeOut":0.5}}
```

- `volume` 是线性增益：`1.0 = 0 dB`、`0.5 ≈ -6 dB`、`2.0 ≈ +6 dB`、`0 = 静音`。
- 动画用关键帧 `content: {"volume": 0.0}`。
- 音频没有舞台 Gizmo；不要尝试在舞台上点选/拖它，从时间轴或 UUID 操作。
- `fadeIn/fadeOut` 是静态秒数，不是关键帧。

### 6.4 HTML（.pvhtml）

目录结构：

```text
pulse/
  index.html
  pvhtml.json
  ...assets
```

`pvhtml.json`：

```json
{
  "entry": "index.html",
  "logicalSize": [320, 320],
  "timeDependent": true,
  "variables": { "pulse": 0, "spin": 0, "alpha": 1 }
}
```

页面协议（宿主每帧调用）：

```js
window.addEventListener("pvforgeframe", event => {
  const { frame, time, vars } = event.detail;
  // 在这里更新 DOM；宿主随后截图
  document.getElementById("badge").style.transform =
    `scale(${1 + (vars.pulse || 0)}) rotate(${vars.spin || 0}deg)`;
  document.getElementById("badge").style.opacity = vars.alpha ?? 1;
});
```

- `variables` 写**数字默认值**；关键帧 `htmlVars` 只能覆盖数值变量。
- `timeDependent: true` 时每帧都可能不同；`false` 时缓存只渲染第 0 帧，不要用它做逐帧动画。
- 导入两步：先 `asset.import`（目录或 zip，`type` 可给 `html`），再 `entity.create`/`asset.assign`。
- 需要 Chrome/Chromium；预览首次启动浏览器会慢几秒。

### 6.5 摄像机

```json
{"name":"Camera","start":0,"end":119,"row":4,
 "content":{"kind":"camera","camera":{}},
 "transform":{"x":0,"y":0,"rotation":0,"scaleX":1,"scaleY":1}}
```

- `preview.render_frame` 传 `cameraUuid` 才走摄像机；不传则用 `view.state.activeCameraUuid`，都没有就全画布。
- 摄像机 transform：`x/y` 是取景中心、`rotation` 旋转、`scale` 缩放（**<1 拉近/裁切，>1 拉远**；1.0 = 画布 1:1）。
- 用整帧关键帧做推拉摇移，例如 0 帧 `scaleX=1` → 60 帧 `scaleX=1.15`。
- 摄像机实体本身不参与画面绘制，只定义取景。

### 6.6 求解示例：星星自转

```python
# 先建好 Star，并在 startFrame 放一个普通整帧关键帧
pv.must("solver.create", {
    "entityUuid": "<STAR_UUID>",
    "effect": "spin",
    "startFrame": 0,
    "params": {"angularVelocity": 3, "direction": 1}   # 3 度/帧
})
pv.must("solver.evaluate", {"entityUuid": "<STAR_UUID>",
                            "frames": [0, 30, 60, 90, 119]})
```

重力示例：

```python
pv.must("solver.create", {
    "entityUuid": "<COMET_UUID>",
    "effect": "gravity",
    "startFrame": 0,
    "initialVelocity": {"x": 120, "y": 0},
    "params": {"targetUuid": "<WELL_UUID>", "strength": 980,
               "elasticity": 0.35, "friction": 0.2, "stopThreshold": 2}
})
```

求解段会覆盖普通关键帧的 transform；不要在显式求解区间里写普通关键帧。

---

## 7. 错误、恢复与协作

| code | 含义 | 正确反应 |
|---|---|---|
| `invalid_argument` | 参数/帧号/字段不合法 | 读 details.allowedRange/allowedFields，修正后重试一次 |
| `conflict` | 同轨重叠、范围越界、锁定、求解冲突 | 改 row/区间/父级，或先删冲突关键帧 |
| `permission_denied` | Git 未启用 / 敏感路径 / 人类拒绝 | 不要重试；请求人类启用 Git 或换路径 |
| `confirm_required` | 删除/外部导入需要人类确认 | 停下来，把 `details.path`/`affected` 告诉人类 |
| `stopped_by_human` | 人类按了急停 | 停止所有写操作，等待人类恢复 |
| `limit_exceeded` | 预览限流、批量超限、素材过大 | 降低频率/数量/尺寸 |
| `request_timeout` | 工具执行超过 15 秒死线，已拒写并回滚 | 缩小单次操作或分批，重读状态后再试一次；不要假设已写入 |
| `not_available` | 工程未打开，或没有可用浏览器/设备/功能 | 先让人类打开工程；其余情况报告人类，不要循环重试 |
| `internal_error` | 工程未打开以外的内部失败 | 读 message；必要时让人类重开工程 |

协作约定：

- 每个成功写调用 = 一个 git commit（纯素材导入可能没有文本 diff）。
- `history.undo` 是内存撤销，不新增 commit；自己改错优先 undo，不要写反向值。
- 人类可能随时急停；不要假设写权限一直存在。
- 人类可能同时操作 GUI；写前重读实体，避免覆盖。
- 不要在 MCP 工具执行中并行发下一个调用。

---

## 8. 完成前自查清单

- [ ] `project.summary` 的 `durationFrames` 覆盖所有实体 `end+1`。
- [ ] 所有实体 `start >= 0`、`end >= start`；子实体在父区间内。
- [ ] 同父级同 row 的闭区间无重叠。
- [ ] 每个关键帧 `frame` 在 `0..duration-1`，无重复帧。
- [ ] 每个关键帧包含全部动画参数；没有把定义参数（字体/颜色/shape/素材引用）写进 `content`。
- [ ] 矢量 `shape` 用整数 0..8。
- [ ] 音频 `volume` 是线性增益，不是 dB。
- [ ] HTML 的 `htmlVars` 只含 manifest 声明的数值变量。
- [ ] 求解起点有普通整帧关键帧；显式区间内没有普通关键帧。
- [ ] 至少预览 0 / 入场 / 高潮 / 出场 / end 五张图。
- [ ] 预览限流已 sleep，错误已处理。
- [ ] 最终导出交给人类 GUI。

---

## 9. 关键源码索引（需要核对语义时）

| 主题 | 文件 |
|---|---|
| 工程/实体磁盘格式 | `src/model/Project.cpp`、`src/model/Entity.cpp`、`src/model/Migrations.h` |
| 整帧关键帧 | `src/model/Keyframe.cpp`、`src/model/Keyframe.h` |
| 内容类型/动画参数 | `src/model/Content.cpp`、`src/render2/Render.cpp` |
| MCP 工具表 | `src/mcp2/Service.cpp` |
| 工具 JSON Schema / 内置指南 | `src/mcp2/SchemaTools.cpp` |
| 关键帧参数校验 | `src/mcp2/KeyframePayload.cpp` |
| 求解参数与校验 | `src/mcp2/SolverTools.cpp`、`src/solver/Solver.h` |
| 素材导入 | `src/mcp2/AssetTools.cpp`、`src/assets/AssetStore.cpp` |
| HTML 渲染协议 | `src/html2/PvhtmlRenderer.h`、`src/assets/Pvhtml.cpp` |
| 预览渲染 | `src/mcp2/PreviewTools.cpp` |
| MCP HTTP/鉴权 | `src/mcp2/HttpServer.cpp`、`src/mcp2/EndpointFile.cpp` |
| 摄像机取景 | `src/render2/Render.cpp`（`cameraView`） |
| 渲染顺序/可见性 | `src/render2/RenderScene.cpp` |
| 工作台缓存 | `src/workbench/MetaBuilder.cpp`、`src/workbench/Cache.cpp` |
