# PVForge 新实体模型设计

> 本文定义新版本的数据模型、时间轴、素材、MCP 与 git 行为。
>
> 旧工程兼容已明确放弃：不迁移、不读取旧 `.pvproj`、不保留旧场景/文件夹/模板模型。
> 旧的 [DESIGN.md](DESIGN.md) 只作为历史参考，与本文冲突时以本文为准。

## 1. 目标

- **git 友好**：一个实体一个文件；改一个实体只产生一个小 diff；素材内容寻址；每次操作自动 commit。
- **AI 友好**：AI 完全通过 MCP 操作工程；实体用 UUID 定位；MCP 提供批量工具、schema、校验和预览截图。
- **Mine-imator 心智模型**：父级、私有时间线、局部坐标、关键帧相对实体、可见/锁定沿用 MI 的工作方式。
- **PR 式时间轴**：从下往上堆，row 0 在最底，row 越高越靠前。

## 2. 工程目录

新工程是一个目录，不是单文件：

```text
<name>.pvproj/
  project.json
  entities/
    <uuid>.json
  assets/
    <sha256>/
      asset.json
      content.<ext>      # 图片/视频/音频
      bundle/            # .pvhtml 目录包
  .git/
  .gitattributes
  .gitignore
```

- 工程目录本身是 git 仓库；新建工程时自动 `git init`。
- 不维护实体索引文件；打开工程时扫描 `entities/*.json`，按 `parentUuid` 组树。
- 实体文件名只由 UUID 决定，改名不移动文件。
- 旧 `.pvproj` 单文件工程不再支持；打开时直接提示“旧工程格式已废弃”。

### 2.1 project.json

```json
{
  "format": "pvforge-project",
  "version": 1,
  "name": "未命名项目",
  "width": 1920,
  "height": 1080,
  "fps": 30,
  "background": "#101014",
  "markers": [
    {
      "id": "uuid",
      "frame": 0,
      "title": "前奏",
      "note": "Markdown 备注",
      "tags": ["music"],
      "color": "#ffbe3c"
    }
  ]
}
```

- 新建工程时询问 fps、分辨率、背景色，并记住上次选择。
- markers 是全局标注，不占行、不渲染、不限制写权限。
- 没有场景对象，没有场景文件夹，没有场景写权限。

## 3. 实体

### 3.1 实体文件

`entities/<uuid>.json`：

```json
{
  "uuid": "0190f...",
  "name": "标题",
  "parentUuid": null,
  "start": 0,
  "end": 90,
  "row": 0,
  "hidden": false,
  "locked": false,
  "inheritVisibility": true,
  "visible": true,
  "physicsInteract": false,
  "transform": {
    "x": 0,
    "y": 0,
    "rotation": 0,
    "scaleX": 1,
    "scaleY": 1,
    "pivotX": 0,
    "pivotY": 0,
    "opacity": 1
  },
  "keyframes": {
    "x": [
      { "frame": 0, "value": 0, "easing": "linear" }
    ]
  },
  "content": {
    "kind": "empty"
  }
}
```

### 3.2 核心属性

| 字段 | 说明 |
|---|---|
| `uuid` | 实体唯一 ID，AI 用它定位；改名/移动/换素材都不变 |
| `name` | 给用户看的名字，可重复；默认取素材名；文字/矢量/空实体/摄像机用类型默认名 |
| `parentUuid` | 父实体 UUID；`null` 表示根实体 |
| `start` / `end` | 全局帧号，整数，半开区间 `[start, end)`；`end > start` |
| `row` | 在所属父级时间线中的行号；每个父级独立，row 0 在最底 |
| `hidden` | 编辑器隐藏；不渲染/不导出，但时间轴可见；可递归应用到后代 |
| `locked` | 编辑器锁定；不可编辑但可选中；可递归应用到后代；MCP 遵守 |
| `inheritVisibility` | 子级是否继承父级的 hidden/visible 状态，默认 true |
| `visible` | 可关键帧的可见值，0/1，默认 1 |
| `physicsInteract` | 物理干涉开关，0/1，默认 0；可关键帧；1 表示参与碰撞 |
| `transform` | 局部坐标；子实体继承父实体变换 |
| `keyframes` | 相对实体的关键帧表；见第 5 节 |
| `content` | 内容槽；空、文字、矢量、摄像机或素材引用 |

### 3.3 内容槽

`content.kind` 五选一：

| kind | 说明 |
|---|---|
| `empty` | 空实体；可以当父级，也可以以后挂素材/内联内容 |
| `text` | 文字内容，内联在实体文件里，不进素材箱 |
| `vector` | 矢量内容，内联在实体文件里，不进素材箱 |
| `camera` | 摄像机；没有素材；用 transform 的 x/y/rotation/scaleX/scaleY 描述取景 |
| `asset` | 引用 sha256 素材：图片、视频、HTML、音频 |

`asset` 额外字段：

```json
{
  "kind": "asset",
  "assetHash": "<sha256>",
  "assetType": "image | video | audio | html",
  "sourceIn": 0,
  "volume": 1.0
}
```

- `sourceIn` 只用于视频/音频：实体 start 处对应素材内的第几帧。
- `volume` 只用于音频；默认 1.0，可打关键帧。
- `assetType` 是缓存；素材缺失时仍知道实体原本是什么类型，能显示占位。

文字内容：

```json
{
  "kind": "text",
  "text": {
    "text": "Hello",
    "fontFamily": "Noto Sans SC",
    "pixelSize": 96,
    "color": "#ffffff",
    "strokeColor": "#000000",
    "strokeWidth": 0,
    "align": 132,
    "lineHeight": 1.0,
    "letterSpacing": 0,
    "padding": 6,
    "boxWidth": 0,
    "boxHeight": 0,
    "bold": false,
    "italic": false,
    "underline": false,
    "vertical": false,
    "verticalRtl": true
  }
}
```

矢量内容：

```json
{
  "kind": "vector",
  "vector": {
    "shape": 3,
    "width": 1200,
    "height": 64,
    "cornerRadius": 32,
    "sides": 5,
    "innerRatio": 0.45,
    "fillColor": "#ffffff",
    "strokeColor": "#000000",
    "strokeWidth": 0
  }
}
```

摄像机内容：

```json
{
  "kind": "camera",
  "camera": {}
}
```

第一版摄像机参数继续用 transform：x/y 是取景框中心，scaleX/scaleY 是画布占比（0.05..1），rotation 是旋转。渲染/导出时由用户选择使用哪个摄像机。

### 3.4 层级与时间

- 任意实体都可以当父级，包括摄像机；私有时间线继续用浮层，交互和现在一致。
- **时间绝对，空间局部**：
  - 子实体 `start/end` 存全局帧号。
  - 父实体在时间上移动 `delta` 帧时，所有后代的 `start/end` 同步加 `delta`；关键帧不动。
  - 子实体 `transform` 是相对父实体的局部坐标；父实体移动/旋转/缩放时，子实体在渲染时继承，不改子实体文件。
- 子实体必须落在父实体 `[start, end)` 内。
- 裁剪父实体时级联裁剪子实体：
  - 子实体越界部分被裁掉，越界关键帧删除；
  - 完全落在新范围外的子实体连同其后代删除；
  - 扩大父实体不会恢复已删除内容。
- 删除父实体级联删除所有后代；素材不自动删除。
- 换父级时 UI 每次询问：
  - **保持世界位置**：重算局部 transform，画面不跳；
  - **保持局部坐标**：实体跳到新父级坐标系。
  - 时间始终是绝对帧号，不因换父级改变。
  - MCP 必须显式给 `mode: "preserveWorld" | "keepLocal"`，不设默认。
  - 换父级前必须校验：不能形成父子环；子实体 `[start,end)` 必须落在新父级范围内。保持世界位置后越界就拒绝，要求先裁剪或改父级，不静默夹取。

### 3.5 变换与轴心

- 通用变换：`x`、`y`、`rotation`、`scaleX`、`scaleY`、`opacity`、`pivotX`、`pivotY`。
- 轴心按 Mine-imator 原版语义，2D 等价公式固定为：
  ```text
  localToWorld(p) = (x, y) + pivot + R(rotation) · (S(scaleX, scaleY) · (p - pivot))
  ```
  即 pivot 是旋转和缩放的固定点；pivot 在左边缘时，缩放锚定左边缘。Gizmo、渲染、MCP 预览必须共用这个公式。
- 子实体渲染顺序：父实体自身内容先画，然后按 row 从小到大画子实体；root 同理。父实体的 row 决定整个子树相对兄弟的顺序。
- 同一父级内，row 0 最底，row 越大越靠上。
- 同一 row 内如果时间重叠，按 start 升序、再按 UUID 排序，后画的在上。新实体默认不会落在会重叠的 row。

### 3.6 Gizmo

2D 版 gizmo，参考 Inkscape 的角旋转 + Office 的对角缩放 + Blender 的第一象限中心缩放方块。

- **方向**：X/Y 轴跟随实体旋转，是局部坐标轴；实体转了，箭头和手柄一起转。
- **移动**：轴心处保留 X/Y 箭头，拖一个箭头只改局部 x 或 y。
- **旋转**：去掉旋转圆环；四个角外侧各有一个弯弯的双箭头，拖任意一个绕轴心旋转。
  - 默认绕实体 pivot 旋转；
  - 按住 Alt 临时改为绕包围盒中心旋转，松开恢复；
  - 按住 Shift 按 15° 步进。
- **Office 角缩放**：四个角内侧是方形缩放手柄，拖动时顶点跟着鼠标，轴心固定在**对角顶点**。
  - 按住 Shift 等比缩放；
  - 拖过对角顶点后允许负 scale，图像翻转。
- **中心缩放**：轴心局部 +X/+Y 方向固定偏移处有一个小方块，拖动它绕轴心缩放。
  - 横向拖动只改 scaleX，纵向只改 scaleY；
  - 按住 Shift 等比；
  - 拖到第二象限只左右翻转，第三象限完全翻转，第四象限只上下翻转。
- **轴心**：pivot 处保留可拖动的白点；拖动后旋转和缩放围绕新轴心。
- **负 scale**：所有实体都允许，包括摄像机；摄像机负 scale 表示取景框翻转，scale 的绝对值仍受 0.05..1 限制。
- **多选/父子**：只显示当前选中实体的 gizmo，不显示多选整体包围盒，也不显示父级整体包围盒。
- **摄像机**：使用同一套 gizmo，额外画相机框和标题。
- **提交**：直接拖拽，实时预览，松开时提交一个操作/一个 commit。

## 4. 时间轴

- **无限长**：水平方向不设固定长度；导出范围由选定摄像机决定。
- **PR 式从下往上**：row 0 在最底，新行向上加。
- 时间轴面板默认显示根实体；双击父实体打开私有时间线浮层，和现在一致。
- 每个父级有自己的 row 空间；子实体的 row 只在父级内有效。
- 新实体默认 row：
  - 在该实体整个 `[start, end)` 范围内，找同级已占用的最高 row，放在 `最高 row + 1`；
  - 范围内没有任何实体时放 row 0；
  - 人类把素材拖到某一行时，使用鼠标所在 row。
- 新实体默认 start：
  - MCP 创建：必须显式给 `start`；
  - 人类点按钮创建：start 对齐播放头；
  - 素材箱拖入：start 对齐鼠标所在帧。
- 新实体默认 end：
  - 视频/音频：`start + 素材时长`；
  - 图片/HTML/文字/矢量/空实体：`start + 1 秒`；
  - 摄像机：`start + 1 秒`，之后可拉长。
- 行号持久化；删除实体不压缩其他实体的 row。
- 关键帧模式、片段模式、框选、多选、删除、拖拽、缩放、滚动条等交互在现有 clip 时间线基础上改，不退回旧文件夹树。
- markers 显示在标尺上，可增删改，不影响时间轴长度。

### 4.1 切分实体

- 在播放头处把实体切成左右两段：左段 `end = 切点`，右段 `start = 切点`；右段生成新 UUID。
- 名字默认沿用原名；右段可自动加后缀（如 `名字 (2)`），也可保持同名（允许重名）。
- 关键帧按裁剪规则处理：左段保留 `< 切点 - start` 的 key，右段把 frame 减去切点偏移并删除越界 key。
- 视频/音频：左段 sourceIn 不变；右段 `sourceIn += 切点 - start`。
- **子树递归切分**：
  - 完全在切点左边的子实体留在左段；
  - 完全在切点右边的子实体移到右段（保持 UUID 或复制，按实现选稳定方案）；
  - 跨切点的子实体递归切分：两边各一份新实体，关键帧/sourceIn 按同样规则处理；
  - 子实体原来在父级私有时间线里的 row 保留。
- 整个切分是一个操作 = 一个 commit = 一次 undo。
- 切点必须在 `(start, end)` 内；在边界上切分是 no-op。
- 求解段跨切点时：左段保留切点前的解算结果，右段从切点重新起一段；具体是重算新起点还是删段，按 5.4 的裁剪规则处理。

## 5. 关键帧

### 5.1 规则

- 关键帧帧号**相对实体 start**，整数。
- 只允许 `0 .. duration-1`，其中 `duration = end - start`。
- 不在 `duration` 处存关键帧；越界写入直接拒绝，并返回允许范围。
- 移动实体：关键帧不动。
- 裁剪 start 右移 `d`：所有关键帧减 `d`，小于 0 的删除；视频/音频 `sourceIn += d`。
- 裁剪 start 左移 `d`：所有关键帧加 `d`；视频/音频 `sourceIn -= d`，不能小于 0；已删除的关键帧不恢复。
- 裁剪 end 左移：删除 `>= 新 duration` 的关键帧。
- 裁剪 end 右移：时长增加，已删除的关键帧不恢复；视频/音频不能超过素材剩余长度。
- 关键帧字段：`frame`、`value`、`easing`、可选 `note`。
- 数值轨道用缓动插值；`visible`、`physicsInteract` 用 step 插值（0/1）。
- 求解段是例外：段内被驱动属性不使用普通关键帧，见 5.4。

### 5.2 轨道清单

所有实体通用：

```text
x, y, rotation, scaleX, scaleY, opacity, pivotX, pivotY, visible, physicsInteract
```

按内容类型追加：

| 内容 | 轨道 |
|---|---|
| 文字 | `fontSize`、`lineHeight`、`letterSpacing`、`strokeWidth`、`padding`、`boxWidth`、`boxHeight` |
| 矢量 | `width`、`height`、`cornerRadius`、`strokeWidth` |
| 音频 | `volume` |
| 摄像机 | 通用 x/y/rotation/scaleX/scaleY；不使用 opacity |
| HTML | 通用轨道；如果 .pvhtml manifest 声明变量，追加 `var.<name>` 数字轨道 |
| 图片/视频/空实体 | 只有通用轨道 |

第一版静态、不可关键帧：颜色、字体、对齐、粗体/斜体/下划线、竖排、矢量 sides/innerRatio。

### 5.3 MCP 接口

按轨道增量操作，不整表覆盖：

```text
keyframes.get(entityUuid, track)
keyframes.set(entityUuid, track, keys)      # 替换这一条轨道
keyframes.setMany([{entityUuid, track, keys}, ...])
keyframes.remove(entityUuid, track, frame)
keyframes.move(entityUuid, track, fromFrame, toFrame)
keyframes.clear(entityUuid, track)
keyframes.setEasing(entityUuid, track, frame, easing)

solver.create(entityUuid, effect, startFrame, endFrame?, params)
solver.update(entityUuid, solverId, patch)
solver.delete(entityUuid, solverId)
solver.list(entityUuid)
solver.evaluate(entityUuid, solverId?, frames[])
```

- 一次只动一条轨道，AI 不会误覆盖其他轨道。
- `keyframes.set` 的 keys 必须全部在 `0..duration-1`；越界返回错误和允许范围。
- 换内容类型但仍在兼容组内时，类型专用轨道保留在文件里但暂不生效；换回来继续用。

### 5.4 求解关键帧（物理求解段）

普通关键帧：起点和终点都由用户给值。  
求解关键帧：给一个起点和物理效果，后续值由解算器实时算；到“求解结束帧”冻结。

- 一个实体只有一条时间轴；求解段按时间顺序排列，**不能重叠**。
- 求解段的 `startFrame` / `endFrame` **相对实体 start**，和关键帧同一基准；移动实体时整段跟着平移。
- 一个求解段只装一个效果：重力 / 公转 / 自转 / 黑洞。
- 求解段由“求解开始帧”和“求解结束帧”夹住：
  - 开始帧是带值的普通关键帧（x/y 或 rotation），也是该段的初始状态；
  - 结束帧只是停止标记，不存值；解算到该帧后速度归零、值保持；
  - 没有结束帧时，段一直解到 clip 最后一帧；只有时间上最后一个求解段允许省略结束帧，要在它后面再建段必须先补结束帧；
  - 段内不能给被驱动属性加普通关键帧；结束帧之后才能继续普通关键帧。
- 段结束后接普通关键帧：求解结束帧的解算位姿作为“隐式左值”，下一个普通关键帧从它开始插值，不能回到段前的旧值或默认值。
- 创建求解段时，如果区间内已有被驱动属性的普通关键帧，直接拒绝并列出冲突帧；不静默删除、不静默覆盖。
- 所有写入路径（`keyframes.set`、`keyframes.setMany`、`keyframes.move`、`entity.update` 的 tracks、裁剪、undo）都必须重新校验“段内不能有普通关键帧”。
- 被驱动属性由效果决定：
  - 重力 / 公转 / 黑洞：`x`、`y`
  - 自转：`rotation`
  - 其他属性仍可用普通关键帧。
- 时间轴显示：求解段用专门颜色带标出，开始/结束用不同标记；被驱动轨道在段内锁定。
- 右侧面板增加“求解关键帧”区：效果选择、参数、轨迹预览、初始向量手柄、每个参数旁的问号说明。
- 裁剪 start 切进求解段：删除该求解段的 start/end 标记（整段失效），同时把新 start 处的解算位姿落成普通关键帧，避免物体跳到默认值或段前旧值。
- 裁剪 end 切进求解段：把结束标记改到新 clip 末帧（冻结在切点），不要简单移除；之后再拉长 clip 不会让物理段复活/继续延长。段完全落在新范围外则删除。
- 移动实体（start/end 同时平移）：求解段随实体平移，解算参数不变。
- 求解器版本存入段；预览和导出使用同一套固定步长解算，保证一致。

#### 目标引用与循环

- 重力/公转/黑洞的 `targetUuid` 必须指向存在的实体；打开工程时校验，悬空引用直接报错（UI 显示“目标缺失”，MCP 返回错误），不能静默用默认值。
- 删除/换父级目标实体时，先返回依赖它的求解段列表；UI 弹确认，MCP 要求显式 `confirm` 或先改目标。
- 公转/黑洞目标可以自身有求解段或父级动画；渲染前做依赖图拓扑排序并检测循环（A 绕 B、B 绕 A 直接报错）。

```json
"solvers": [
  {
    "id": "uuid",
    "effect": "gravity | orbit | spin | blackhole",
    "startFrame": 0,
    "endFrame": 60,
    "version": 1,
    "initialVelocity": { "x": 0, "y": 0 },
    "params": {
      "targetUuid": "地面/公转中心/黑洞实体 UUID"
    }
  }
]
```

#### 重力

- 方向：目标实体的局部下方（局部 +Y）；目标旋转时重力跟着转。
- 参数：重力强度、初速度、弹性、摩擦、停止阈值。
- 初速度在轨迹预览里用手柄拖；初速度和采样速度都在**求解空间**（画布/父级空间），单位 px/s。
- 碰撞：运动物体和所有 `physicsInteract=1` 的实体碰撞（排除运动物体自己）；目标地面自己也要 `physicsInteract=1` 才参与碰撞。矢量用实际路径，图片/文字/HTML 用包围盒。
- 碰撞响应：按法线反弹，弹性控制速度保留，摩擦削减切向速度，速度低于停止阈值就停住。

#### 公转

- 中心：公转对象实体（`targetUuid`）。
- 椭圆必定经过求解开始帧坐标；用户调扁率（短轴/长轴）和椭圆旋转角（相对公转对象局部），椭圆大小由中心到起点距离反解；实际 a/b 只读显示。
- 方向：顺/逆时针；速度：帧/圈。
- 公转对象可以是任意实体；每帧取它当前位置和旋转；检测循环依赖。
- 起点决定初始相位；周期从起点开始算。

#### 自转

- 参数：角速度（度/帧）+ 方向。
- 轴心用实体 pivot；初始 rotation 取开始帧的值。
- 只驱动 rotation，不改 x/y。

#### 黑洞

- 参数：黑洞实体（`targetUuid`）、模拟速度、质量、角动量、事件视界半径、初始速度、物体半径、引力软化半径、最大速度。
- 初速度手柄可拖；速度够大可以逃离黑洞。
- 只改 x/y；不改大小、rotation、可见性。
- 事件视界不是硬边界：物体可以穿过视界继续飞；软化半径避免中心无穷大力。
- 角动量产生切向拖拽，轨迹呈旋涡；参数旁有问号说明。

## 6. 素材

### 6.1 标识与存储

- 素材没有 UUID，只有 **sha256 内容哈希**。
- `assets/<sha256>/asset.json` 存元数据；内容文件放同目录。
- 相同内容重复导入按 sha256 去重，只保留一个素材；默认名用第一次导入的名字，可手动改名。
- 实体只引用 `assetHash`；换素材只改这一行。
- 素材被删除后，引用它的实体显示“素材缺失”占位；重新导入相同内容会自动恢复。
- 删除仍被引用的素材不阻止，按 PR 行为显示缺失；MCP 返回缺失列表。

`asset.json` 示例：

```json
{
  "hash": "<sha256>",
  "type": "image | video | audio | html",
  "name": "background.png",
  "mime": "image/png",
  "width": 1920,
  "height": 1080,
  "durationFrames": 300,
  "sampleRate": 48000,
  "channels": 2,
  "entry": "index.html",
  "variables": {}
}
```

### 6.2 类型与兼容组

| 组 | 类型 | 说明 |
|---|---|---|
| 视觉素材 | 图片、视频、HTML | 可以互相换素材 |
| 生成内容 | 文字、矢量 | 内联在实体文件里，可以互相换内容类型 |
| 音频 | 音频 | 只能换音频 |
| 摄像机 | 摄像机 | 不能换素材 |

- 空实体可以接受任意组的内容；换回空实体只清空内容，保留 UUID、时间、行、变换、关键帧和子实体。
- 换素材时如果新素材有时长且比实体短：实体 end 自动裁到 `start + 新素材时长`，越界关键帧删除。
- 图片/HTML 没有固有时长，换过去时保留实体当前时长。
- 视频/音频不能把 end 拉到超出 `sourceIn + 素材时长`。

### 6.3 视频与音频

- 导入视频时自动拆出：
  - 视频实体：引用视频素材；
  - 音频实体：引用拆出的 **WAV 无损** 音频素材。
- 两个实体初始 start/end 对齐，但之后**完全独立**：移动/裁剪/删除视频不会联动音频，删除视频也不提示、不自动删除同源音频。
- 音频实体只用于视频原声、BGM、配音；没有隐式配对关系。
- 音频混合按实体 `volume` 轨道/静态值。

### 6.4 HTML

- HTML 素材是 `.pvhtml` **目录包**：`index.html` 在根，CSS/JS/图片等资源在包内；可导入 zip 并自动解包成目录。
- 目录包整体算一个 sha256 素材，存放在 `assets/<sha256>/bundle/`。
- 可选 `pvhtml.json` manifest：逻辑宽高、变量定义、是否时间相关。
- 渲染使用外部无头 Chrome（DevTools 协议）：
  - 允许 JS；
  - 禁网络；
  - 禁访问包外本地文件；
  - 按帧截图，缓存键为 `assetHash + frame + 变量值 + 输出尺寸`。
- 找不到 Chrome 时 HTML 层显示错误占位，其他功能不受影响。
- MCP 创建 HTML 强制两步：先 `asset.import` 导入 `.pvhtml`，再用 `entity.create` 或 `asset.assign` 引用 assetHash；不允许 `entity.create` 直接吃 `.pvhtml` 路径。

## 7. MCP

### 7.1 原则

- AI **完全通过 MCP** 读写工程；工程文件格式不承诺作为 AI 写接口。
- MCP 默认全工程可写；删除父级、批量删除、删除素材等危险操作要求显式 `confirm: true`。
- 一次 MCP 工具调用 = 一个逻辑操作 = 一个 commit。
- 批量工具直接在一次调用里操作多个实体，不需要额外的 batch 开关。
- MCP 遵守 `locked`：写锁定实体会返回错误；解锁需要显式操作。
- 所有帧号参数都是整数帧；关键帧帧号是相对实体的。

### 7.2 工具面

```text
project.get
project.summary
project.update

entity.create
entity.createTree
entity.createMany
entity.get
entity.list
entity.update
entity.updateMany
entity.move
entity.trim
entity.reparent
entity.duplicate
entity.delete
entity.deleteMany

keyframes.get
keyframes.set
keyframes.setMany
keyframes.remove
keyframes.move
keyframes.clear
keyframes.setEasing

asset.import        # 本地路径 -> 复制进工程
asset.list
asset.get
asset.assign
asset.unassign
asset.rename
asset.delete

markers.list
markers.set
markers.remove

preview.render_frame
view.state

schema.get
guide.get

history.undo
history.redo
history.log
```

### 7.3 批量工具

- `entity.createTree`：一次创建父实体 + 子实体 + 关键帧 + 行号，返回所有 UUID。
- `entity.createMany`：一次创建多个不相关实体。
- `entity.updateMany` / `keyframes.setMany` / `entity.deleteMany`：一次处理多个对象。
- 批量工具内部要么全部成功，要么整次调用回滚；失败不会留下部分数据。

### 7.4 预览

`preview.render_frame` 返回：

- 当前帧或指定帧的 PNG（路径 + 可选 base64）；
- 当前播放头帧；
- 当前选中实体 UUID；
- 当前打开的私有时间线 owner UUID；
- 当前视图范围/缩放；
- 当前使用的摄像机 UUID（可传参覆盖）。

`view.state` 只返回 UI 状态，不渲染图片。

## 8. Git

### 8.1 依赖

- `git` 和 `git-lfs` 是**硬依赖**：启动时查找；找不到就阻止新建/打开/保存，并提示安装。
- 新建工程自动 `git init`，自动写 `.gitattributes` 让图片/视频/音频走 Git LFS。
- 工程目录是独立仓库；不自动 push，不自动切分支。

### 8.2 提交粒度

- 人类的一次操作 = 一个 commit：鼠标释放、输入框失焦、按钮点击、一次拖拽。
- 一次 MCP 工具调用 = 一个 commit。
- 批量工具一次调用只 commit 一次。
- 提交消息自动生成，例如：
  - `pvf: create entity "标题"`
  - `pvf: create tree "标题" (4 entities)`
  - `pvf: set keyframes "标题" x`
  - `pvf: delete 3 entities`
  - `pvf: import asset "bg.png"`
  - `pvf: save`
- 只提交本应用写过的工程路径，不把用户手动改的其他文件卷进 commit。
- commit 失败时报告错误，不静默继续；工程文件保留，允许重试。
- 人类 UI 和 MCP 写入走**同一个串行命令队列**：同一时刻只有一个写事务，避免两边同时改同一实体或抢 git index 锁。MCP 连接断开时取消未完成事务。

### 8.3 撤销

- 不做内存命令栈；`Ctrl+Z` = `git revert` 最近一个操作 commit。
- `Ctrl+Shift+Z` = revert 那次 revert。
- 一次操作一个 commit，所以一次 undo 回退一个完整操作。
- 批量工具是一个 commit，所以一次 undo 回退整个批量操作。
- 提供 git 历史面板，可查看/回退到任意提交。

## 9. 渲染与导出

- 导出范围 = 渲染时选定的摄像机实体的 `[start, end)`。
- 没有摄像机时导出报错，提示先创建/选择摄像机。
- 渲染顺序：root 按 row 升序；每个实体先画自身内容，再按 row 升序画子实体。
- 变换：父世界矩阵 × 子局部矩阵；pivot 按 MI 语义；opacity 沿父链相乘。
- 可见性：`hidden`、`visible` 轨道、`inheritVisibility` 共同决定；父级不可见时子级按继承规则隐藏。
- 视频/音频：源帧 = `sourceIn + (当前帧 - start)`；超出素材范围时按实体裁剪规则不会发生。
- HTML：无头 Chrome 按帧截图。
- 音频：混合所有在范围内且可见的音频实体。
- 导出沿用现有 MP4 管线，音频混流。

## 10. 明确不做

- 旧工程迁移/读取。
- 场景对象、场景文件夹、场景写权限。
- 素材 UUID。
- 视频与音频的自动配对联动（只在导入时拆出，之后独立）。
- 内存命令栈撤销（用 git revert）。
- 关键帧颜色/字体/枚举插值（第一版只做数字和布尔轨道）。
- 求解轨迹烘焙成普通关键帧（运行时实时解算）。
- 自动 push、自动切分支。

## 11. 待细化

- 文字/矢量/摄像机完整字段 schema 与默认值。
- Gizmo 双箭头/中心方块的像素尺寸、命中范围和像素风外观。
- 求解器每个参数的单位、默认值、问号说明文案；碰撞/椭圆/黑洞的数值实现。
- MCP 各工具的完整参数 schema（后续按具体操作补）。
- .pvhtml manifest 字段与 Chrome 缓存失效规则。
- git revert 历史面板的交互细节。
- 私有时间线浮层、关键帧模式、多选/框选的 UI 细节。
- 旧文档更新/删除计划。
