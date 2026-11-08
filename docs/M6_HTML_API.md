# HTML 组件契约（M6）

> 面向组件作者和通过 MCP 写组件的 AI。工作流与安全边界见 [AI_AUTHORING.md](AI_AUTHORING.md)，方案与阶段拆分见 [M6_PLAN.md](M6_PLAN.md)。
>
> 状态：M6 文档草案。本文定义 `pvforge-html` v1 的资产格式、运行时 API 和确定性要求；Stage 1 实现以本文为准，实现中如有偏差会先改本文。

## 1. 资产格式

一个 HTML 组件就是一个 JSON 文件，内容寻址存为 `assets/html/<sha256>.json`：

```json
{
  "format": "pvforge-html",
  "version": 1,
  "html": "<!doctype html><html>...</html>",
  "manifest": {
    "size": [800, 120],
    "transparent": true,
    "timeDependent": false,
    "components": [
      {
        "id": "progress",
        "name": "进度条",
        "variables": [
          {
            "name": "value",
            "type": "number",
            "min": 0,
            "max": 100,
            "default": 0,
            "integer": true
          }
        ]
      }
    ]
  }
}
```

字段：

| 字段 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `format` | string | 是 | 固定 `pvforge-html` |
| `version` | integer | 是 | 固定 `1`；更高版本拒绝加载 |
| `html` | string | 是 | 完整 HTML 文档，必须以 `<!doctype html>` 开头 |
| `manifest` | object | 是 | 尺寸、透明、时间相关性和组件/变量声明 |

规则：

- `html` 是**一整份文档**，不是片段；宿主不拼接 `<div>` 或 `<script>`。
- 文件必须是 UTF-8 JSON；HTML 里的资源全部内联，不能有外部 URL。
- 资产是**只读素材**：应用内不提供代码编辑器；替换时整份资产替换，不修改单个片段。
- 引用了资产的实体不能直接删除资产；先换掉引用或移除实体（人类操作）。
- 资产 JSON ≤ 5MB，其中 `html` 字符串 ≤ 2MB；超限拒绝导入。

## 2. manifest

### 2.1 顶层字段

| 字段 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `size` | `[integer, integer]` | 是 | 逻辑像素尺寸 `[宽, 高]`，都 > 0；抓帧按这个宽高比 |
| `transparent` | boolean | 是 | `true` 表示页面背景透明，抓帧保留 alpha；`false` 表示组件自己铺满背景 |
| `timeDependent` | boolean | 是 | 输出是否依赖 `pvf.frame` / `pvf.time`；`false` 时宿主可按变量缓存帧 |
| `components` | array | 是 | 1–16 个组件声明 |

`timeDependent` 必须诚实：

- 只由变量决定 → `false`；
- 读取 `pvf.frame` / `pvf.time`，或内部按帧计数 → `true`。
- 声明 `false` 却按帧变化，会导致预览/导出复用到错误的缓存帧，属于契约违规。

### 2.2 组件与变量

每个组件：

```json
{
  "id": "progress",
  "name": "进度条",
  "variables": [
    {"name": "value", "type": "number", "min": 0, "max": 100, "default": 0, "integer": true}
  ]
}
```

| 字段 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `id` | string | 是 | 组件标识，层内唯一；只含字母、数字、`_`、`-`，不要用点 |
| `name` | string | 是 | 属性面板显示名，可中文 |
| `variables` | array | 是 | 0–N 个变量；静态组件可为空数组 |

变量：

| 字段 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `name` | string | 是 | 变量名，组件内唯一；只含字母、数字、`_`，不要用点 |
| `type` | string | 是 | v1 只支持 `number` |
| `min` | number | 是 | 最小值，必须小于 `max` |
| `max` | number | 是 | 最大值 |
| `default` | number | 是 | 没有关键帧时的值；会被夹到 `[min, max]` |
| `integer` | boolean | 否 | 只是 UI 提示（步进/显示取整），内部仍是 double，宿主不做量化 |

规则：

- 所有值在求值后夹到 `[min, max]`，再传给组件。
- 变量的完整名字是 `<component id>.<variable name>`，例如 `progress.value`；工程里的轨道名是 `var.progress.value`。
- 组件 id / 变量名重复、`min >= max`、未知 `type`、缺少必填字段 → 资产非法（`invalid_asset`）。
- 未声明的变量不会出现在 `pvf.vars` 里；组件不要读取未声明的键。
- 变量数量不单独设上限，但受 5MB 资产上限约束。

## 3. `pvf` 运行时 API

宿主在组件脚本执行**之前**注入全局对象 `window.pvf`，并先填好所有变量的 `default` 值。组件只能通过它读当前帧，不要访问宿主内部对象。

| API | 类型 | 说明 |
|---|---|---|
| `pvf.vars` | object | 当前帧所有变量值，只读；键是 `<component id>.<variable name>`，值是 double。每帧由宿主整体更新 |
| `pvf.frame` | integer | 当前帧号 |
| `pvf.time` | number | 当前秒数，`frame / 工程帧率` |
| `pvf.width` | integer | manifest 的逻辑宽度 |
| `pvf.height` | integer | manifest 的逻辑高度 |
| `pvf.asset(name)` | string | 返回工程内资产的 URL（宿主已内联为 `data:` 或内部 URL）；未知名称返回空字符串并记日志 |
| `pvf.ready()` | function | 初始化完成；宿主等到它之后才开始抓第一帧 |
| `pvf.rendered()` | function | 本帧渲染完成；宿主等到它 + 两次 `requestAnimationFrame` 后抓帧 |
| `pvf.render(vars)` | function（可选） | 宿主更新 `vars`/`frame`/`time` 后调用；组件在这里同步更新 DOM/Canvas，并在结束时调用 `pvf.rendered()` |

约定：

- `pvf.vars`、`pvf.frame`、`pvf.time` 每帧由宿主写入；组件**不要修改** `pvf.vars`。
- 每帧重新读 `pvf.vars`，不要把它缓存到闭包外；`pvf.render(vars)` 的参数与 `pvf.vars` 是同一份数据。
- 有变量的组件必须定义 `pvf.render`；否则变量变化后组件不会更新。
- `pvf.render` 必须是同步的：更新 DOM/Canvas 后立刻调用 `pvf.rendered()`，不要在里面 `await` 网络/字体/图片。
- 没有变量的纯静态组件可以不定义 `render`；宿主仍会等待 `ready` 和两次 rAF。
- 不要替换整个 `pvf` 对象，只挂 `pvf.render`。
- `pvf.asset(name)` 的 `name` 是工程素材名/键，不是文件路径；资产必须已经内联，组件不能自己发请求。

### 3.1 初始化与单帧流程

首次加载：

1. 宿主创建独立页面，注入 `pvf`；
2. 加载 HTML；
3. 等 `pvf.ready()`；
4. 等 `document.fonts.ready`；
5. 等两次 `requestAnimationFrame`；
6. 抓第一帧。

之后每一帧：

1. 宿主写入 `pvf.vars`、`pvf.frame`、`pvf.time`；
2. 如果存在 `pvf.render`，调用 `pvf.render(pvf.vars)`；
3. 等 `pvf.rendered()`；
4. 等两次 `requestAnimationFrame`；
5. `Page.captureScreenshot`（透明背景按 manifest）；
6. 解码、按实体变换合成到画面。

超时（默认 1 秒）内没有等到 `ready` / `rendered`，视为渲染失败。

## 4. 确定性要求

组件必须是**当前帧的纯函数**：输出只由 `pvf.vars`、`pvf.frame`、`pvf.time` 和静态资产内容决定。

- 同一帧连续渲染两次，PNG 像素哈希必须一致。
- 同一工程导出两次，同一帧解码后的像素必须一致。
- 不依赖鼠标、键盘、窗口尺寸、滚动位置、网络状态、当前时间。
- 不依赖上一帧遗留的 DOM/Canvas 状态；每帧从 `vars` 重新算，Canvas 每帧 `clearRect`。
- 允许缓存，但缓存只能加速，不能改变输出；缓存键必须包含所有影响画面的输入。

### 4.1 禁止

- CSS `animation`、`transition`、`@keyframes`；宿主会注入覆盖样式，依赖它们的组件会静止或行为异常。
- 自由的 `requestAnimationFrame` 循环；rAF 只能用于等布局，不能自己推进动画。
- `setTimeout` / `setInterval` 驱动画面。
- `Date.now()`、`performance.now()`、`new Date()`。
- `Math.random()`；宿主虽按帧注入种子，但组件不应依赖随机数。
- 网络：`fetch`、XMLHttpRequest、WebSocket、EventSource、动态 `import()`、外部 `<script>` / `<link>` / 字体 / 图片。
- 本地文件：`file://`、绝对路径、`<input type=file>`。
- 媒体：`<video>`、`<audio>`、`<canvas>` 里的视频帧、Web Audio。
- WebGL / WebGPU / 3D 上下文。
- iframe、Worker、Service Worker、插件。
- 用 `localStorage` / IndexedDB / Cookie 保存跨帧画面状态。

### 4.2 字体与图片

- 字体必须内联：`@font-face` 的 `src` 用 `data:` URL，或只用通用字族（`sans-serif` 等）。
- 跨机器严格一致时请内联字体；通用字族在不同系统上字形/度量可能不同。
- 图片/SVG 用 `data:` URL，或通过 `pvf.asset` 取工程内资产；不能引用 `http(s)://`、`file://`。
- `<img>` 必须在 `pvf.ready()` 之前完成加载/解码；Canvas 里用的图片同理。不要在 `render` 里等图片。
- 宿主在首次加载后等 `document.fonts.ready`，但组件自己的异步加载仍要在 `ready` 前完成。

### 4.3 跨机器差异

以下 CSS 效果可能因 GPU/软件渲染产生细微差异，跨机器复现优先避免：

- `backdrop-filter`；
- 依赖合成层的 `mix-blend-mode`；
- 大面积 `filter: blur()`；
- 系统字体回退。

v1 的 Canvas 按**逻辑像素**绘制；导出放大时由宿主缩放位图。需要高清导出优先用 DOM/SVG；`pvf` 目前不暴露抓帧倍率。

## 5. 允许的技术范围

| 技术 | v1 | 说明 |
|---|---|---|
| HTML / DOM | 允许 | 静态结构，文本、布局、显隐 |
| CSS | 允许（静态） | 布局、颜色、渐变、`clip-path`、`mask`、transform、静态 filter |
| SVG | 允许 | 内联 `<svg>` 或 data URL；内部也不能有动画 |
| Canvas 2D | 允许 | 同步绘制；每帧清空重绘 |
| WebGL / WebGPU | 禁止 | — |
| 视频 / 音频 | 禁止 | — |
| iframe / Worker | 禁止 | — |
| 外部资源 | 禁止 | 所有字体/图片/脚本/样式必须内联 |

## 6. 尺寸、资源与抓帧限额

| 项目 | 上限 |
|---|---|
| 单层 `html` 字符串 | 2MB |
| 单资产 JSON | 5MB |
| 单层组件数 | 16 |
| 同时 HTML 层数 | 4 |
| 抓帧边长 | 4096px |
| 每层在途抓帧 | 1（新帧覆盖旧帧） |
| 单帧渲染超时 | 默认 1 秒 |
| MCP `preview.render_frame` | 2 次/秒（服务端限流） |

超限行为：

- 导入时超限 → `limit_exceeded`，资产不落库。
- 运行时抓帧尺寸超限 → 该层渲染失败，预览占位，导出报错。
- 单层同时只允许一个在途抓帧；连续更新只保留最新一帧。

## 7. 错误与超时

| 场景 | 行为 | 错误码（MCP/日志） |
|---|---|---|
| JSON 不合法、`format`/`version` 不对、缺字段、组件/变量重复、`min >= max` | 拒绝导入，不落库 | `invalid_asset` |
| 超出尺寸/组件/资产限额 | 拒绝导入或拒绝抓帧 | `limit_exceeded` |
| 没有可用 Chrome/Edge | 已有 HTML 层显示占位框；新建层禁用；导出报错 | `browser_unavailable` |
| 初始化 `ready` 超时、JS 异常 | 该层预览占位并记日志；工程数据不变 | `render_failed` |
| 单帧 `rendered` 超时 | 该帧失败；预览占位；导出中止 | `render_timeout` |
| 抓帧返回空图/解码失败 | 该帧失败；导出中止，不写坏帧 | `render_failed` |
| 浏览器崩溃 | 占位 + 日志，自动重启一次；连续失败禁用该层 | `render_failed` |

规则：

- 预览失败只影响显示，不修改工程；不要把占位图当成组件输出继续导出。
- 导出任何一帧失败都立即停止，删除 `.part.mp4`，不输出错帧。
- 错误消息应带组件 id、帧号、层名，方便 AI 定位。
- AI 收到 `render_timeout` / `render_failed` 后应停止，不要连续抓帧重试。

## 8. 与时间线变量的绑定

- 工程轨道名：`var.<component id>.<variable name>`，例如 `var.progress.value`。
- 属性面板按组件分组，行显示 `组件名.变量名`；范围来自 manifest。
- 内部一律 double；`integer` 只影响 UI 步进/显示，不影响插值。
- 插值复用 `Track` + `EasingSpec`：
  - 线性；
  - 瞬间（保持到关键帧再跳变）；
  - 30 种预设缓入/缓出/缓入缓出；
  - 自定义三次贝塞尔。
- **缓动属于到达的关键帧**：给第 72 帧设曲线，影响上一关键帧→72 帧这一段；第一帧没有进入段。
- 没有关键帧用 `default`；只有一个关键帧时全程恒定；求值后夹到 `[min, max]`。
- AI 工具：
  - `html.set_variables`：设置当前变量值（走自动关键帧逻辑）；
  - `variables.set_keyframes`：批量设置变量关键帧；
  - `easing.set`：设置到达关键帧的缓动。
- 换资产时按变量全名匹配：同名轨道保留关键帧，新变量用默认值，旧轨道保留但标记“未使用”。
- `timeDependent: true` 时缓存键包含资产 hash + 抓帧尺寸 + 变量值 + frame/time；`false` 时只包含前三项。

## 9. 最小骨架

```html
<!doctype html>
<html>
<head>
<meta charset="utf-8">
<style>
html, body { margin: 0; width: 800px; height: 120px; background: transparent; overflow: hidden; }
</style>
</head>
<body>
<div id="out"></div>
<script>
(function () {
  'use strict';
  var out = document.getElementById('out');

  function render() {
    var vars = pvf.vars || {};
    var v = Number(vars['demo.value']);
    if (!isFinite(v)) v = 0;
    out.textContent = v.toFixed(0);
    pvf.rendered();
  }

  pvf.render = render;
  render();
  pvf.ready();
})();
</script>
</body>
</html>
```

对应的最小 manifest：

```json
{
  "size": [800, 120],
  "transparent": true,
  "timeDependent": false,
  "components": [
    {
      "id": "demo",
      "name": "示例",
      "variables": [
        {"name": "value", "type": "number", "min": 0, "max": 100, "default": 0, "integer": true}
      ]
    }
  ]
}
```

完整示例见 [`examples/html/`](../examples/html/)：`progress-bar.json`、`lower-third.json`、`karaoke.json`。

## 10. 提交前检查表

- [ ] `format` 是 `pvforge-html`，`version` 是 1，`html` 以 `<!doctype html>` 开头。
- [ ] `manifest.size` 和 HTML 实际布局尺寸一致。
- [ ] `transparent` 与 HTML 的 `background` 一致；需要透明时 `html, body` 都是 `transparent`。
- [ ] `timeDependent` 与是否读取 `frame`/`time` 一致。
- [ ] 每个变量都有 `min`/`max`/`default`，`min < max`，`default` 在范围内。
- [ ] 组件 id / 变量名合法且唯一；轨道名是 `var.<id>.<name>`。
- [ ] 有变量时定义了 `pvf.render`，并在结束调用 `pvf.rendered()`。
- [ ] 初始化结束调用 `pvf.ready()`。
- [ ] 没有 CSS 动画/过渡、自由 rAF、定时器、`Date.now`、`Math.random`。
- [ ] 没有网络、文件、媒体、WebGL、iframe、Worker、外部字体/图片。
- [ ] 字体/图片已内联；Canvas 图片在 `ready` 前完成解码。
- [ ] 同一帧连续渲染两次像素一致；中间变量值画面正确。
- [ ] 资产 JSON ≤ 5MB，`html` ≤ 2MB，组件 ≤ 16。
