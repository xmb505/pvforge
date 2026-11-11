# Inkscape 选择框旋转手柄研究

> 目的：给 PVForge 旧 UI 的 Gizmo 提供可复用的实现依据。
> 本文只读研究 Inkscape 源码，不改 PVForge 代码。

## 0. 研究基线

| 项 | 值 |
| --- | --- |
| 仓库 | `https://gitlab.com/inkscape/inkscape.git` |
| 本地路径 | `/tmp/inkscape-src`（浅克隆） |
| commit | `cc683d5d234382197e90950f0f7d89b29f995d09` |
| 提交时间 | 2026-05-12 |
| 版本 | `1.5.0-dev`（`CMakeLists.txt` 第 43–46 行） |
| 对照安装版 | Inkscape 1.4.3（本机） |

Inkscape 里有**两套**变换手柄实现，行为基本一致：

1. **选择工具（Selector）**：`Inkscape::SelTrans` + `SPKnot`，这是用户最常看到的一套。本文以它为主。
2. **节点工具（Node）**：`Inkscape::UI::TransformHandleSet` + `ControlPoint`，是较新的重构版本，代码里明确注释“这段逻辑在 seltrans.cpp 里重复了一份”。

旋转手柄不是“一个在框外的单独手柄”，而是**每个角各一个 90° 弯折的双箭头图标**（curved double arrow）。旋转模式下四角是旋转箭头，四边中点是斜切（skew）箭头，框中心是旋转中心十字。

---

## 1. 关键文件与符号

### 1.1 选择工具（主实现）

| 职责 | 文件 | 符号 / 行号 |
| --- | --- | --- |
| 手柄描述表（26 个） | `src/seltrans-handles.cpp` | `hands[]`，第 18–45 行 |
| 手柄类型 / 结构体 | `src/seltrans-handles.h` | `SPSelTransType` 第 28 行；`SPSelTransHandle` 第 74 行；`NUMHANDS = 26` 第 81 行 |
| 模式状态机 | `src/seltrans.cpp` | `increaseState()` 第 202 行 |
| 开始变换、记录 bbox/对边点 | `src/seltrans.cpp` | `grab()` 第 235 行 |
| 显示/隐藏手柄、摆放位置 | `src/seltrans.cpp` | `_updateHandles()` 第 670 行；`_showHandles()` 第 739 行 |
| 创建 26 个 `SPKnot` | `src/seltrans.cpp` | `_makeHandles()` 第 763 行 |
| 手柄按下 / 拖动 / 释放 | `src/seltrans.cpp` | `handleGrab()` 第 907 行；`handleNewEvent()` 第 927 行；`handleRequest()` 第 973 行 |
| 旋转中心选择（中心 or 对角） | `src/seltrans.cpp` | `originRequest()` 第 955 行 |
| 旋转角度计算 + 吸附 | `src/seltrans.cpp` | `rotateRequest()` 第 1365 行 |
| 应用相对仿射 | `src/seltrans.cpp` | `commitRelativeAffine()` 第 1500 行 → `transform()` 第 365 行 |
| 再次点击已选对象切换模式 | `src/ui/tools/select-tool.cpp` | `handleClick()` 第 1024 行；其中 `increaseState()` 调用第 1050 行；状态提示第 78 行 |
| Shift+S 切换模式 | `src/ui/tools/select-tool.cpp` | 第 953 行 |
| 手柄事件 / 拖动阈值 | `src/ui/knot/knot.cpp` | `eventHandler()` 第 104 行；`startDragging()` 第 82 行；`/options/dragtolerance` 第 116、204 行 |

### 1.2 手柄的几何与绘制

| 职责 | 文件 | 符号 / 行号 |
| --- | --- | --- |
| 控件锚点、屏幕像素偏移、命中框 | `src/display/control/canvas-item-ctrl.cpp` | `_update()` 第 373 行；箭头偏移公式第 405–421 行；`contains()` 第 188 行 |
| 手柄尺寸计算 | 同上 | `get_width()` 第 265 行；`get_total_width()` 第 271 行；`set_size_via_index()` 第 253 行 |
| 弯箭头路径 | `src/display/control/ctrl-handle-rendering.cpp` | `draw_carrow()` 第 98 行；`draw_cairo_path()` 第 386 行；`draw()` 第 543 行 |
| 形状枚举 | `src/display/control/canvas-item-enums.h` | `CANVAS_ITEM_CTRL_SHAPE_CARROW` 第 35 行（注释即 “Double headed curved arrow”） |
| CSS 默认样式 | `share/ui/node-handles.css` | `.inkscape-adj-rotate { shape: "curved-arrow" }` 第 188–190 行；尺寸第 160–166 行 |
| CSS 解析 | `src/display/control/ctrl-handle-styling.cpp` | `ctrl_type_map` 第 48 行；`ctrl_shape_map` 第 90 行；`"'curved-arrow'"` 第 102 行 |
| 控件拾取顺序 | `src/display/control/canvas-item-group.cpp` | `pick_item()` 第 91 行（倒序遍历，后加的在上） |
| overlay 层创建顺序 | `src/desktop.cpp` | `_setupCanvasItems()` 第 140–179 行；`CanvasItemGroup:Controls` 最后创建 |

### 1.3 节点工具的新实现（可对照）

`src/ui/tool/transform-handle-set.cpp`

| 职责 | 行号 |
| --- | --- |
| `RotateHandle::computeTransform()` | 398–480 |
| `RotationCenter`（可拖动的中心） | 637–680 |
| `setBounds()` 摆放 4 角 / 4 边 / 中心 | 744–769 |
| 拖动时隐藏其它手柄、显示虚线框 | `_setActiveHandle()` 771；`_clearActiveHandle()` 782 |
| 根据 bbox 尺寸隐藏无意义手柄 | `_updateVisibility()` 791–850 |
| 15° 吸附 `snap_angle()` / `snap_increment_degrees()` | 54–59 / 61–66 |

---

## 2. 手柄数据模型

`SPSelTransHandle` 只有四个字段：

```
type    // HANDLE_SCALE / STRETCH / ROTATE / SKEW / CENTER / 对齐
anchor  // SP_ANCHOR_SE 等，决定图标朝向和偏移方向
control // 旧图片编号，现已基本不用
x, y    // bbox 归一化坐标，0..1
```

`hands[]` 里旋转相关的条目：

| 类型 | anchor | x | y |
| --- | --- | --- | --- |
| `HANDLE_ROTATE` | SE | 0 | 1 |
| `HANDLE_SKEW` | S | 0.5 | 1 |
| `HANDLE_ROTATE` | SW | 1 | 1 |
| `HANDLE_SKEW` | W | 1 | 0.5 |
| `HANDLE_ROTATE` | NW | 1 | 0 |
| `HANDLE_SKEW` | N | 0.5 | 0 |
| `HANDLE_ROTATE` | NE | 0 | 0 |
| `HANDLE_SKEW` | E | 0 | 0.5 |

摆放时 `_showHandles()` 做的是：

```
bpos = (x, (y - 0.5) * (-y_dir) + 0.5)
pos  = bbox.min + bbox.dimensions * Scale(bpos)
knot.moveto(pos)
```

`y_dir` 处理画布 Y 轴向上/向下的差异。也就是说 **knot 的位置就是 bbox 的角点本身**，图标的“往外/往内偏移”不是在 SelTrans 里做的，而是在 `CanvasItemCtrl::_update()` 里按 anchor 加的屏幕像素偏移。

---

## 3. 模式切换与显示时机

`SelTrans::_state` 三态：

```
STATE_SCALE     // 缩放/拉伸
STATE_ROTATE    // 旋转/斜切
STATE_ALIGN     // 画布上对齐（可选）
```

`increaseState()`：

```
SCALE -> ROTATE
ROTATE -> ALIGN   (仅当 /dialogs/align/oncanvas = true)
其它   -> SCALE
```

触发方式：

- 已选中对象上再点一下：`SelectTool::handleClick()` 里调用 `increaseState()`；
- `Shift+S`：同样调用 `increaseState()`；
- 状态栏提示原文：`"Click selection again to toggle scale/rotation handles"`。

`_updateHandles()` 每次先 `hide()` 全部 26 个 knot，再按状态显示：

| 状态 | 显示的手柄 |
| --- | --- |
| `STATE_SCALE` | 4 个 scale 角 + 4 个 stretch 边 |
| `STATE_ROTATE` | **4 个 rotate 角 + 4 个 skew 边 + 1 个 center** |
| `STATE_ALIGN` | 4 个 side align + 4 个 corner align + 1 个 center align |

注意：旋转模式不是“只显示 4 个弯箭头”，四边中点的斜切箭头同时存在；缩放模式则完全不显示旋转箭头和中心十字。

拖动开始后 `grab(..., show_handles=false)`，所有 knot 隐藏，只显示：

- `_norm`：`CANVAS_ITEM_CTRL_TYPE_CENTER`，画在旋转原点；
- `_grip`：`CANVAS_ITEM_CTRL_TYPE_MARKER`，画在当前鼠标/手柄位置。

这就是 Inkscape 拖动旋转时“框和手柄都消失，只剩中心十字和一个跟随鼠标的记号”的原因。

---

## 4. 手柄的屏幕布局与恒定尺寸

### 4.1 bbox 来源

- `/tools/bounding_box = 0` → `SPItem::VISUAL_BBOX`（含描边）
- `/tools/bounding_box = 1` → `SPItem::GEOMETRIC_BBOX`

`_updateVolatileState()` 和 `grab()` 里取 `selection->bounds(type)`。bbox 是**文档坐标下的轴对齐矩形**，不是对象自身的旋转矩形。

### 4.2 控件偏移（关键）

`CanvasItemCtrl::_update()` 对箭头类形状（`DARROW` / `SARROW` / `CARROW` / 对齐箭头）统一处理：

```
angle = int(anchor) * PI/4
if (affine().flips() == context->yaxisdown())
    angle = -angle
angle += angle_of(affine())

half = total_width / 2
dx = -(half + 2) * cos(angle)
dy = -(half + 2) * sin(angle)

// CARROW 还要再转 5*PI/4，让弯箭头贴着角
angle += 5*PI/4
```

最终位置：

```
pos   = (dx, dy) + position * affine()
bounds = Rect(pos - half, pos + half).roundOutwards()
```

几个直接结论：

- **偏移量是屏幕像素**（`half + 2`），不是文档单位；`position` 才是文档坐标。所以缩放画布时手柄大小和离角点的距离都不变。
- 对 `CARROW`，`dx/dy` 的方向是**朝 bbox 内侧的对角线**：例如 NE 角 `angle = 7π/4`，`dx = -0.707*(half+2)`、`dy = +0.707*(half+2)`，即手柄中心向左下（框内）移动。
- 手柄中心到角点的对角线距离是 `half + 2`，而图标外接框半宽是 `half`，所以图标外接框最多只比角点越出约 2px；视觉上弯箭头是“骑在角上”的，不是悬在框外。
- `affine().flips() == yaxisdown()` 这一行只处理**画布/视图翻转**和 Y 轴向上；对象自身的负 scale 不会让图标镜像（见第 8 节）。

### 4.3 手柄尺寸

默认 CSS（`node-handles.css`）：

```
* { scale: 2.0; size-extra: 1px; stroke-width: 1.0px; outline-width: 1.45px; }
.inkscape-adj-handle, .inkscape-adj-skew, .inkscape-adj-rotate,
.inkscape-adj-salign, .inkscape-adj-calign, .inkscape-adj-malign,
.inkscape-adj-center {
    fill: black;
    size-extra: 7px;
    stroke-scale: 0;
}
```

`CanvasItemCtrl::get_width()`：

```
width = _width * style.scale() + style.size_extra()
```

`_width` 来自 `/options/grabsize/value`（默认 3，范围 1..15），`set_size_via_index()` 会把它夹到 1..15。默认：

```
_width      = 3
width       = 3 * 2.0 + 7 = 13 px
total_width = 13 + stroke(1.0) + 2 * outline(1.45) ≈ 16.9 px
```

所以默认旋转图标的外接框约 **17×17 px**；`grabsize` 调到 1 时约 12.9px，调到 15 时约 41px。这个尺寸与 zoom 无关。

### 4.4 旋转图标自身的旋转角

`CARROW` 的最终旋转角是：

```
glyph_angle = anchor * 45° + 225°
```

四个角依次相差 90°，所以每个角的弯箭头都“抱住”该角，两个箭头沿两条邻边切向伸出。`affine` 翻转时角度取反，保证画布翻转后图标仍然朝外。

PVForge 的选择框会跟着实体旋转，所以不能直接用固定的 anchor 角度，而是取当前框角的屏幕内角平分线 `A`：

```
glyph_angle = A + 225°
```

对未旋转的框，`A` 正好等于 `anchor * 45°`，两种写法一致；实体绕 Y-up 正方向（屏幕逆时针）转 `θ` 时，`A` 减少 `θ`，`QPainter::rotate()` 的角度也随之减少，图标才会和角一起转。不要再额外加上实体旋转角，也不要在旋转后补 180°。

---

## 5. 弯箭头到底怎么画（无 SVG path）

现代 Inkscape **没有** `rotate.svg` 之类的静态资源，也没有用 SVG path 字符串。`draw_carrow()` 直接用 Cairo 构造一条闭合路径，然后依次做 outline / fill / stroke 三遍。

参数（`S` = 图标宽度，单位是设备像素）：

```
delta = round(1.5 + (S - 3) / 4) - 1.5
tip   = (1.5, 1.5 + delta)
out   = (1.5 + delta, 1.5)
in    = (1.5 + delta, 1.5 + delta/2)
arc_c = (1.5 + delta, S - 1.5 - delta)
r_out = S - 3 - 1.5*delta
r_in  = S - 3 - 2.5*delta
```

路径顺序（伪代码）：

```
move_to(tip)
line_to(out)              // 第一枚箭头的外侧边
line_to(in)               // 箭头内侧凹口
arc(arc_c, r_out, 270°, 360°)   // 外弧，扫 90°
line_to(...)              // 第二枚箭头的尖端
line_to(...)              // 第二枚箭头外侧
line_to(...)              // 第二枚箭头内侧凹口
arc_negative(arc_c, r_in, 0°, 270°)  // 内弧，反向扫回
line_to(...)              // 回到第一枚箭头
close_path()
```

几个可复用的数值特征：

- 弧是 **90°**，不是两个 120° 的圆弧；
- 外弧半径与内弧半径之差等于 `delta`，即箭杆厚度等于箭头长度；
- 两枚箭头分别位于角的两条邻边上，尖端朝切向；
- 整个图形是**一条闭合路径**，可以用一次 fill + 一次 stroke 画出；
- 默认是黑填充 + 深色描边 + 白色外轮廓（outline），所以在深浅背景上都可见；
- 绘制结果按 `(shape, fill, stroke, outline, size, angle, device_scale)` 缓存在 `ctrl-handle-rendering.cpp` 的 `cache` 里，不重复构建。

如果要移植到 Qt，`QPainterPath::arcTo` 的起始角/扫掠角约定与 Cairo 的弧度 `arc` 不同（Qt 用角度、0° 在 3 点钟方向）；最稳的做法是按上面的控制点在**本地坐标**里手工构造路径（90° 圆弧可用三次贝塞尔近似，控制点系数 0.5523），再整体 `QPainter::rotate`，不要直接照抄 Cairo 的角度参数。

---

## 6. 命中测试、光标、拖动阈值

### 6.1 命中框

`CanvasItemCtrl::contains()`：

```
if (!_bounds) return false;
if (tolerance == 0) return _bounds->interiorContains(p);
else return closest_distance_to(p) <= tolerance;
```

拾取时 `CanvasItemGroup::pick_item()` 调用的是默认 `tolerance = 0`，所以**命中区域就是 `_update()` 算出的轴对齐正方形**：

```
[pos.x - total_width/2, pos.x + total_width/2] × [pos.y - total_width/2, pos.y + total_width/2]
```

它不关心图标旋转了多少，也不做圆形距离测试。默认约 17×17 px，比图标本身略大（因为包含 stroke/outline）。

`pick_item()` 倒序遍历子项（后添加的先命中），并且只考虑 `visible && pickable && contains(p)`。选择手柄都在 `CanvasItemGroup:Controls` 里，这个组在所有绘图组之后创建，所以永远在最上层。

### 6.2 光标

`SPKnot` 的 `_cursors[]` 默认是空的，`handleGrab()` 也没有给选择手柄设置专用光标。结果是：

- 悬停/拖动旋转手柄时使用**选择工具默认光标**（箭头），没有“旋转”光标；
- 悬停时通过 `defaultMessageContext()` 在状态栏显示提示文字；
- 真正的 `rotate.svg` 光标只用于 **Ctrl+拖动旋转画布**（`src/desktop-events.cpp` 第 282、319 行），不是选择手柄。

### 6.3 拖动阈值

`SPKnot::eventHandler()`：

```
tolerance = prefs->getIntLimited("/options/dragtolerance/value", 0, 0, 100);
if (within_tolerance && LInfty(event.pos.floor() - xyp) < tolerance)
    return;  // 还没超过阈值，不开始变换
```

默认 0 px，即按下后只要移动就立刻开始。超过阈值后才发 `grabbed_signal`，之后所有 motion 都按真实坐标处理，不会“吸回”起点。

---

## 7. 旋转拖动算法

### 7.1 拖动开始：记录快照

`handleGrab()` 调用 `grab(knot->position(), handle.x, handle.y, false, false)`。

`grab()` 里与旋转有关的状态：

```
_point   = 手柄按下时的位置（bbox 角点，文档坐标）
_bbox    = selection->bounds(...)
_opposite = bbox.min + bbox.dimensions * Scale(1 - x, 1 - y)
_center  = selection->center()   // 由 _updateHandles() 预先算好；显式 center 或 bbox 中点
```

对 SE 角（x=0, y=1），`_opposite` 是 `(1, 0)` 即 NW 角；对 NW 角则是 SE 角。也就是**对角点**。

### 7.2 选择旋转中心

`originRequest()` 的逻辑是一个 XOR：

```
off_center = Shift 是否按下
if (!is_stkey() && off_center == (_state == STATE_ROTATE)) {
    _origin = _opposite;          // 旋转模式 + Shift -> 绕对角
} else if (_center) {
    _origin = *_center;           // 其它情况 -> 绕中心
}
```

所以在选择工具里：

- 旋转模式，不按 Shift：绕**旋转中心**（默认 bbox 中点，可拖动）；
- 旋转模式，按 Shift：绕**当前手柄的对角点**；
- 缩放模式，按 Shift：绕旋转中心对称缩放（同样的 XOR 逻辑）。

`_center` 不是临时算的：`SPItem` 有 `transform_center_x/y`，拖动中心十字会写进所有选中对象；`ObjectSet::center()` 优先返回第一个对象的显式 center，否则返回 bbox 中点（`src/object/object-set.cpp` 第 435 行）。

### 7.3 角度计算

`rotateRequest()` 的核心：

```
d1 = _point - _origin          // 按下时手柄相对 pivot 的向量
d2 = pt - _origin              // 当前鼠标相对 pivot 的向量

h1 = |d1|; if (h1 < 1e-15) return FALSE;
h2 = |d2|; if (h2 < 1e-15) return FALSE;
q1 = d1 / h1; q2 = d2 / h2;

radians = atan2(dot(rot90(d1), d2), dot(d1, d2));
```

`dot(rot90(d1), d2)` 就是二维叉积，所以这是**带符号的夹角**，范围 (-π, π]。在屏幕 Y 向下的坐标系里，正角度视觉上是顺时针。

### 7.4 吸附与步进

```
increments = Alt 是否按下      // TRANS_INCREMENT
confine    = Ctrl 是否按下     // TRANS_CONFINE

if (increments || confine) {
    cos_t = dot(q1, q2);
    sin_t = dot(rot90(q1), q2);
    radians = atan2(sin_t, cos_t);
    radians = (PI / snaps) * floor(radians * snaps / PI + 0.5);
    r1 = Rotate(0);
    r2 = Rotate(radians);
} else {
    // 对选中的 snap points 做 PureRotateConstrained 吸附
    // 吸到对象/网格/参考线时用 sn.getAngleSnapped()
}
```

`snaps` 来自 `/options/rotationsnapsperpi/value`，默认 **12**：

```
180° / 12 = 15°
```

即默认 15° 步进。范围 0.1..1800，所以可以配成其它步长。

注意 `floor(x + 0.5)` 对负半格的处理：`-7.5°` 会吸到 `0°`，`+7.5°` 会吸到 `15°`，半格平局偏向正方向。节点工具的新实现改用 `round()` 并夹到 `[-π, π]`（`transform-handle-set.cpp` 第 54–59 行），更对称。

### 7.5 生成仿射并应用

```
r1 = Rotate(q1)          // 初始方向
r2 = Rotate(q2)          // 当前方向
_relative_affine = r2 * r1.inverse()
```

`commitRelativeAffine()` → `transform(_relative_affine, _origin)`，后者做：

```
affine = Translate(-norm) * rel_affine * Translate(norm)
```

对每个选中对象：

```
item.set_i2d_affine(prev_i2d_affine * affine)
```

即**所有对象共享同一个世界空间旋转**，每个对象保留自己的 `i2d` 变换。旋转过程中 `_center` 不跟着转（它就是 pivot）。

状态栏每次更新 `_last_angle`，显示 `Rotate by %.2f°`。

### 7.6 伪代码汇总

```
on_press(handle):
    snapshot bbox, center, opposite, handle_pos
    origin = shift ? opposite : center
    d1 = handle_pos - origin

on_drag(mouse):
    d2 = mouse - origin
    angle = atan2(cross(d1, d2), dot(d1, d2))
    if ctrl or alt:
        angle = round_to_step(angle, 15°)
    else:
        angle = snap_to_objects(angle, origin)
    rel = Rotate(angle)
    apply_world_affine(rel, origin)   // 相对按下时的快照，不是增量累积

on_release:
    commit undo, restore handles
```

关键点：**角度永远从按下时的 `d1` 算起，`_relative_affine` 是“总旋转”而不是每帧增量**；这样拖动过程中不会累积误差，也不会因为跨过 ±π 跳变。

---

## 8. 负 scale / 翻转时的行为

Inkscape 的选择框是**文档坐标下的轴对齐 bbox**，手柄锚在 bbox 的四个视觉角上，而不是对象局部坐标的四个角。因此：

- 对象被负 scale 镜像后，bbox 仍然是轴对齐矩形，四个旋转手柄仍然在**视觉上的四个角**；
- 手柄身份（哪个是“NW 手柄”）由 `hands[]` 的 anchor 决定，不随对象翻转交换；
- `CanvasItemCtrl::_update()` 只在**画布/视图翻转**（`affine().flips() == yaxisdown()`）时对图标角度取反，对象自身的 `scaleX/scaleY < 0` 不会让弯箭头镜像；
- 旋转角度的符号仍然由屏幕上的叉积决定，所以镜像对象拖同一个角，视觉旋转方向与未镜像时一致。

节点工具的 `TransformHandleSet` 还多了一层保护：

```
bool y_inverted = !desktop->yaxisdown();
for i in 0..3:
    d_c = y_inverted ? i : 3 - i
    _scale_corners[i] = new ScaleCornerHandle(*this, i, d_c)
    _rot_corners[i]   = new RotateHandle(*this, i, d_c)
```

以及 `_updateVisibility()`：

- bbox 宽或高接近 0 时隐藏缩放/旋转手柄；
- `bp = bbox.dimensions * current_zoom`，当两个角手柄之间的屏幕距离小于 `handle_size` 时隐藏边手柄，避免手柄叠在一起。

选择工具的旧实现没有这层隐藏逻辑，退化 bbox 上四个手柄会重叠，这是旧代码的已知粗糙点。

---

## 9. 绘制顺序与 overlay 层

`SPDesktop::_setupCanvasItems()` 按顺序创建根级 group（后面的在上层）：

```
CanvasItemCatchall        // 最底
CanvasItemGroup:PagesBg
CanvasItemGroup:Drawing
CanvasItemGroup:PagesFg
CanvasItemGroup:Grids
CanvasItemGroup:Guides
CanvasItemGroup:Sketch
CanvasItemGroup:Temp
CanvasItemGroup:Controls  // 最上层：手柄、knot、选择框、snap 指示
```

- 选择手柄、旋转中心、`_norm`/`_grip`、选择框虚线都在 `getCanvasControls()` 里；
- `CanvasItemGroup::_render()` 正序遍历子项，后添加的后画 → 在 `Controls` 内部，`_grip`/`_norm` 比 26 个 knot 晚创建，所以画在它们上面；
- `pick_item()` 倒序遍历，所以 `_grip`/`_norm` 的命中优先级也高于普通 knot；
- 旋转拖动时 `_setActiveHandle()` 会隐藏其它手柄、显示一个虚线 `CanvasItemRect`（节点工具）；选择工具则是隐藏全部 knot，只留中心标记和鼠标标记。

对 PVForge 的启示：**只要 gizmo 画在场景内容之后、并且命中测试在内容命中之前，就已经等价于 Inkscape 的 overlay 层**，不需要额外的窗口层。

---

## 10. 与 PVForge 当前实现的差异

PVForge 相关代码：

- `src/gizmo/Gizmo.h` / `Gizmo.cpp`：纯数学层
- `src/ui/Viewport.cpp`：绘制、命中、拖动
- 常量：`kHitRadius = 7.0`、`kScaleHandleInset = 10.0`、`kRotateHandleOffset = 16.0`、`kMoveAxisLength = 48.0`、`kCenterScaleOffset = 28.0`（`Gizmo.h` 第 82–86 行）

| 维度 | Inkscape | PVForge 现状 | 影响 |
| --- | --- | --- | --- |
| 手柄布局 | 4 角旋转箭头 + 4 边斜切箭头 + 中心十字；缩放/旋转模式互斥 | 4 角缩放方块 + 4 角旋转图标 + 中心缩放 + 两条移动轴，全部同时显示 | PVForge 信息更密，旋转图标与缩放方块容易视觉/命中冲突 |
| 旋转图标位置 | 以 bbox 角点为锚，沿外侧对角线偏移约 `0.707*(half+2)`；图标外接框只越出角点约 2px | 沿“角点→变换后中心”方向向外 16px | Inkscape 是“骑角”，PVForge 是“悬在框外”，视觉语言不同 |
| 图标形状 | 一条闭合路径：90° 外弧 + 90° 内弧 + 两枚三角箭头；有白 outline | 两段 120° 圆弧（`drawArc`），无箭头，1.4px 浅色描边 | PVForge 更像“旋转提示环”，不是 Inkscape 的弯双箭头 |
| 图标尺寸 | 约 17px（默认 grabsize） | 外接 14px（半径 7） | 接近，但 Inkscape 的箭头头部占比更大 |
| 命中区域 | 轴对齐正方形，边长 ≈ 17px；无额外 tolerance | 圆形，半径 7px | 角上斜向点击时 PVForge 更容易漏判 |
| 命中优先级 | 模式互斥，不会重叠 | 缩放角 → 中心缩放 → 旋转角 → 移动轴 | 小尺寸时旋转图标可能被缩放方块抢走 |
| 光标 | 工具默认箭头；状态栏提示 | 拖动开始时 `Qt::CrossCursor`；无 hover 变化 | 交互反馈不同 |
| 模式切换 | 再次点击对象 / Shift+S | 无模式概念，全部手柄常驻 | 若要保持旧 UI，不应强行加入模式切换 |
| 旋转中心 | 每个对象持久化的 center，默认 bbox 中点；中心十字可拖动，Shift 绕对角 | `transform.pivot` 即旋转中心；Alt 切到 bbox 中心 | 语义不同：PVForge 的 pivot 同时是缩放锚点 |
| 修饰键 | Shift = 绕对角；Ctrl/Alt = 15° 步进；中心拖动 Ctrl 约束轴 | Alt = 绕 bbox 中心；Shift = 角度吸附（3° 容差）；Ctrl = 关闭对象吸附 | 不建议直接照搬，需按旧 UI 决定 |
| 吸附 | 固定 `/options/rotationsnapsperpi` 15°，另有对象/网格/参考线吸附 | `appSettings().snapAngles` 列表 + 3° 容差 + Shift 强制 | PVForge 更灵活，但缺少“硬步进”语义 |
| 角度算法 | `atan2(cross, dot)`，从按下快照算总角度 | `atan2` 差值 + `remainder`，同样从快照算 | 数学等价，PVForge 已正确 |
| 负 scale | 手柄跟随视觉 bbox 角；图标不镜像 | 手柄绑定 local corner（`localCorner`），负 scale 后视觉角互换 | 翻转后“左上角手柄”会跑到视觉右下，图标朝向也可能不直观 |
| 屏幕恒定尺寸 | 位置和尺寸都在屏幕像素空间 | 偏移 `pixels / viewScale`，`viewScale = sqrt(abs(det))` | 非均匀父变换下 PVForge 的偏移会变形/偏大偏小 |
| 绘制层 | `CanvasItemGroup:Controls` 最上层 | `paintEvent` 末尾直接画，等价 overlay | 结构不同但效果等价 |

---

## 11. 对 PVForge 可复用的实现建议

以下建议只改内部绘制/命中/数学，不改变旧 UI 的交互语义。

### 11.1 用“闭合弯双箭头”替换两段圆弧

- 按 `draw_carrow()` 的控制点构造一条 `QPainterPath`：外弧 90°、内弧 90°、两枚三角箭头，一次 fill + 一次 stroke（可选白色 outline）。
- 图标本地坐标固定为 `S×S`（建议 S = 14~17），四个角分别旋转 `corner_index * 90°`，让箭头沿两条邻边切向伸出。
- 这样即使不加入“旋转模式”，四角的旋转图标也能一眼和缩放方块区分开。

### 11.2 把 gizmo 几何完全移到 widget/屏幕空间

- 现在 `cornerHandlePosition()` 在 local 空间里除以 `viewScale`（`Gizmo.cpp` 第 127 行），而 `viewScale = sqrt(abs(det))` 只是平均缩放；父级有非均匀 scale 或剪切时，16px 偏移会变形。
- 更稳的做法：在 `Viewport::drawSessionGizmo()` 里先把角点和 bbox 中心 map 到 widget，再在 widget 空间沿“角点→中心”方向偏移固定像素；`handlePosition()` 只负责返回文档坐标的角点/中心。
- 命中测试同样在 widget 空间做，旋转手柄用 **16~18px 的方形框**（或 `QPainterPath` 的 `contains`），比半径 7 的圆更接近 Inkscape 的手感。
- 如果保留局部空间 API，至少把 `viewScale` 拆成 X/Y 两个分量，避免非均匀变换下偏移方向错误。

### 11.3 旋转交互补齐 Inkscape 的三个语义（按需开关）

1. **绕对角旋转**：拖动开始时记录**对角点**（bbox 的 opposite corner，不是 `2*pivot - corner`，因为 pivot 可能不在 bbox 中心），按住 Shift 时以它为 pivot；这与 Inkscape 的 `originRequest()` 一致，且不需要改 UI 布局。
2. **15° 硬步进**：Ctrl/Alt 时把角度吸到 `round(angle / 15°) * 15°`，用 `round` 而不是 `floor(x+0.5)`，保证正负对称；现有的 `snapAngles` 列表可以作为“软吸附”，两者不要混在一起。
3. **拖动时隐藏四角手柄，只留 pivot + 光标标记**：Inkscape 在 `_setActiveHandle()` 里做这件事，PVForge 现在拖动时仍画全部手柄，视觉噪音大，也容易让用户误以为还能抓别的角。

### 11.4 负 scale 下的手柄身份

如果希望“左上角旋转图标永远在视觉左上”，需要在 `handlePosition()` / `hitTest()` 里根据 `scaleX/scaleY` 的符号交换 `Handle::RotateTopLeft` 等枚举，或者改成纯视觉角索引（0..3）+ 局部角映射。当前实现里 `Handle::RotateTopLeft` 绑定的是**局部**左上角：`scaleX = -1` 时它会出现在视觉右上角。命中位置本身没错，但手柄身份和用户看到的角不对应；如果以后给图标加上方向性箭头，朝向也会跟着局部轴而不是视觉角。

### 11.5 命中优先级

Inkscape 用“模式互斥”彻底避免角上重叠。PVForge 若保持全部手柄常驻，建议：

- 旋转手柄命中框放在缩放方块**外层**，两者重叠时优先旋转（或按距离最近者）；
- 小尺寸 bbox 时隐藏中心缩放/移动轴，只保留四角；
- hover 时改变图标颜色或加 outline，给用户“这个角现在是旋转”的反馈。

---

## 12. 参考文件清单

Inkscape：

```
src/seltrans.cpp
src/seltrans.h
src/seltrans-handles.cpp
src/seltrans-handles.h
src/ui/tools/select-tool.cpp
src/ui/knot/knot.cpp
src/ui/tool/transform-handle-set.cpp
src/display/control/canvas-item-ctrl.cpp
src/display/control/ctrl-handle-rendering.cpp
src/display/control/ctrl-handle-styling.cpp
src/display/control/canvas-item-group.cpp
src/display/control/canvas-item-enums.h
share/ui/node-handles.css
src/desktop.cpp
src/enums.h
src/object/object-set.cpp
src/object/sp-item.cpp
```

PVForge：

```
src/gizmo/Gizmo.h
src/gizmo/Gizmo.cpp
src/ui/Viewport.cpp
src/ui/Viewport.h
src/gizmo/tests/gizmo_tests.cpp
```
