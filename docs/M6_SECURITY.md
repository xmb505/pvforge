# M6 安全与架构红队评审

记录时间：2026-11-08
评审对象：[M6_PLAN.md](M6_PLAN.md)、`src/core/Project.*`、`src/core/History.*`、`src/media/MediaImporter.*`、`src/ui/CameraMonitor.*`、`src/app/SettingsDialog.*`
评审方式：静态阅读 + 威胁建模；未运行主程序、未做模糊测试。文中“现有计划”指 M6_PLAN.md 当前文本。

## 0. 结论

M6 的安全方向是对的：只读默认、场景边界、批次快照、急停、内容寻址资产。但当前计划把多项**设计意图**写成了**安全承诺**，缺少运行时验证和失败关闭（fail-closed）语义。按现在的文字直接实现，最可能出事的不是“AI 乱改工程”，而是：

1. CDP 通过本地端口暴露，任何本机进程（包括其他用户）可无认证控制浏览器、截取工程画面、导航到 `file://`。
2. “断网/禁 file/禁 WebGL”主要依赖 Chrome 启动参数，而 Chrome 会静默忽略未知参数；IP 直连、WebSocket、WebRTC、代理环境变量都能绕过。没有启动自检就不是不变量。
3. MCP 只读/场景边界只在 `tools/list` 或参数层做，执行层没有按“目标实体的根祖先 == 当前场景根”复核；人类移动播放头会改变 AI 的写入范围。
4. 急停、checkpoint、History 三者没有定义提交点和竞态；急停可能回滚到一半，或回滚后 Ctrl+Z 把旧状态复活。
5. 不可信 `.pvproj` 的加载没有大小、实体数、路径和图片尺寸上限；`Project::load()` 直接 `readAll()`，绝对路径和 `..` 都会被接受。

结论：**当前计划不足以支撑“安全地让 AI 操作工程”这句话**。需要把第 3 节的承诺逐条改成可自动验证的不变量，并优先补第 4 节的 P0 项。

## 1. 信任边界与资产

| 对象 | 信任级别 | 说明 |
|---|---|---|
| PVForge 主进程 | 可信 | 所有权限判断、路径解析、checkpoint 必须以它为准 |
| 本机同一用户的其他进程 | 不可信（但无法完全防御） | 能读 0600 文件、能连 127.0.0.1、能改用户配置；只能缩小窗口，不能假设安全 |
| 本机其他用户 | 不可信 | 0600/0700、loopback + token 主要防这一层；注意 loopback 端口对本机所有用户可见 |
| MCP 客户端 / AI | 不可信 | 可能被 prompt injection、恶意插件或用户误配置控制；只能通过 schema 和权限层约束 |
| HTML 组件 | 不可信 | 等价于在用户机器上运行不受信任的网页脚本；即使来源是“AI 写的”也不可信 |
| `.pvproj` / 资产文件 | 可能不可信 | 从网盘、聊天、Download 目录打开；v3 工程也可能引用任意绝对路径 |
| Chrome/Edge 二进制 | 有条件可信 | 必须是用户确认过的规范路径；更新后行为可能变化，需要每次启动自检 |
| checkpoint / MCP endpoint 文件 | 高敏感 | 含完整工程、注释/歌词、token；按秘密文件处理 |

需要保护的资产：工程 JSON、注释/歌词/场景备注、渲染帧、素材路径与用户名、MCP token、checkpoint 历史、用户本地文件系统、用户浏览器配置（绝不能被读取）、GPU/CPU/磁盘。

## 2. 威胁清单

编号用于优先级表和验收清单。每条给：触发、影响、计划覆盖、缺口、缓解、验收。

### 2.1 HTML 组件（H）

#### H1 组件网络外传

- 触发：组件执行 `fetch('http://127.0.0.1:8080/...')`、`new WebSocket()`、`navigator.sendBeacon()`、`<img src>`、`@import`、`EventSource`、WebRTC data channel；或读取继承的 `http_proxy` 走攻击者代理。
- 影响：把 `pvf.vars`、当前帧画面、工程结构（若被注入）发到外网；对 `127.0.0.1` 上的本地服务发起 SSRF/CSRF（MCSManager、路由器管理页、开发服务器）；下载二次载荷。
- 计划覆盖：部分。§1.7 写“Chrome 启动参数 + CDP 拦截所有非 data:/内联请求”。
- 缺口：Chrome 没有可靠的“全局断网”开关；`--host-resolver-rules` 不影响 IP 字面量；`Fetch` 域不覆盖 WebSocket/WebRTC；继承的 `http_proxy`/`HTTPS_PROXY` 会把流量导向代理；Chrome 自身后台联网（组件更新、variations）不受页面 CSP 约束。
- 缓解：
  1. 组件永远放在 `sandbox="allow-scripts"` 的 iframe 里（不加 `allow-same-origin`、`allow-popups`、`allow-top-navigation`、`allow-downloads`），宿主 shell 只负责截图；组件文档内注入 CSP：`default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; img-src data: blob:; font-src data:; connect-src 'none'; media-src 'none'; object-src 'none'; frame-src 'none'; worker-src 'none'; base-uri 'none'; form-action 'none'`。
  2. Chrome 参数：`--proxy-server=http://127.0.0.1:9` + `--proxy-bypass-list=<-loopback>` + `--host-resolver-rules=MAP * ~NOTFOUND` + `--disable-background-networking` + `--disable-component-update` + `--no-pings` + `--dns-prefetch-disable`；子进程环境清空 `http_proxy/HTTPS_PROXY/ALL_PROXY/NO_PROXY`。
  3. Linux 优先在 `setChildProcessModifier` 里 `unshare(CLONE_NEWNET)`（或 `bwrap --unshare-net`），这是唯一能同时封死 WebRTC/原始套接字/IP 字面量的方案；失败则降级并提示。
  4. CDP `Fetch.enable` 只作为兜底：除首个文档请求外全部 `Fetch.failRequest`；不要把它当唯一防线。
  5. 页面里不放任何秘密：只注入当前层当前帧的 `pvf.vars`，不注入工程路径、其他场景、注释、token。即使外传成功，损失也限于当前帧公开内容。
  6. 启动 canary：本地起一个只监听 127.0.0.1 的诱捕 HTTP/WebSocket 服务，让测试页尝试 fetch/WS/img/WebRTC；只要收到一个连接，就判定断网失败并禁用 HTML 渲染（fail-closed）。
- 验收：`--m6-security-test` 中 canary 连接计数必须为 0；`fetch('file:///etc/hostname')`、`fetch('http://127.0.0.1:<canary>')`、`new WebSocket('ws://127.0.0.1:<canary>')` 全部 reject；页面内 `document.cookie` 为空、`localStorage` 抛异常或不可用。

#### H2 本地文件读取、下载与剪贴板

- 触发：`fetch('file:///home/user/.ssh/id_rsa')`、`<iframe src="file:///...">`、`location='file:///...'`、`<a download>`、`navigator.clipboard.writeText()`、`showSaveFilePicker()`。
- 影响：读取 SSH 密钥、浏览器配置、其他工程；向 Downloads 写文件；覆盖剪贴板（诱导用户粘贴恶意内容）。
- 计划覆盖：部分。§1.7 写“禁止 file://、本地路径、下载、媒体”。
- 缺口：Chrome 没有“禁用 file scheme”的可靠参数；沙箱 iframe 能阻止父页面访问，但组件仍可能自导航；`--disable-gpu` 不影响下载；剪贴板权限未提。
- 缓解：
  1. 组件只存在于 `srcdoc`/`about:srcdoc` 的 sandbox iframe 中，顶栏文档由宿主控制；`pvf.asset()` 只返回 `data:` URL，永不返回 `file://` 或绝对路径。
  2. `Browser.setDownloadBehavior({behavior:'deny'})`；`Browser.setPermission` 对 clipboard-read/write、geolocation、notifications、midi、camera、microphone 全部 deny；参数加 `--disable-features=FileSystemAccessAPI`（若版本支持）。
  3. 监听 `Page.frameNavigated`：组件 frame 的 URL 一旦不是 `about:srcdoc` 或宿主分配的 blob，立即 `Target.closeTarget`。
  4. 启动 canary 验证：`location='file:///etc/hostname'` 后 URL 不变、没有新 target；下载事件计数为 0。
- 验收：canary 页面尝试上述 API 后，宿主记录 0 次导航、0 次下载、剪贴板内容不变。

#### H3 Chrome 沙箱逃逸或沙箱未启用

- 触发：恶意 HTML 利用 Chrome 0-day；或 Linux 上 `chrome-sandbox` 缺失、user namespace 被禁用，应用为了“能跑”偷偷加 `--no-sandbox`；或用户以 root 运行。
- 影响：浏览器渲染进程逃逸为当前用户代码执行，前面所有 HTML 隔离失效。
- 计划覆盖：无。计划只写“外部 Chromium/CDP”，没有沙箱要求和失败策略。
- 缺口：没有“沙箱不可用则禁用 HTML”的硬规则；没有禁止 `--no-sandbox` 的检查；没有 root 检测。
- 缓解：
  1. 代码中不存在 `--no-sandbox`、`--disable-setuid-sandbox`、`--single-process`、`--no-zygote` 字面量；启动前断言。
  2. Linux 检测：`chrome-sandbox` 为 root:root 且 setuid，或 `kernel.unprivileged_userns_clone=1`（或 `user.max_user_namespaces>0`）；两者都无则 HTML 禁用并给安装说明。macOS/Windows 依赖 Chrome 自带沙箱；Windows 用户级安装也保留沙箱。
  3. `geteuid()==0` 时拒绝启动 Chrome（或明确要求非 root 用户运行）；不要用 `--no-sandbox` 换取可用性。
  4. 严格模式可选：`--js-flags=--jitless`（牺牲性能，降低 JIT 漏洞面）；默认关闭并文档说明。
  5. 进程加固：`setChildProcessModifier` 里 `setsid()`、`umask(077)`、`RLIMIT_CORE=0`、关闭除 CDP 管道外的 fd；`--disable-breakpad --disable-crash-reporter`，避免崩溃转储带走工程画面。
- 验收：测试断言命令行不含禁用沙箱参数；在无 userns 的容器里启动，应得到“HTML 不可用”而不是静默降级；`/proc/<renderer>/status` 中 `NoNewPrivs`/`Seccomp` 符合预期（Linux）。

#### H4 无限循环、挖矿与 Worker 放大

- 触发：`while(true){}`、`for(;;)requestAnimationFrame(...)`、WebAssembly 死循环、`setInterval` 高频、`new Worker()` 起 16 个线程。
- 影响：单核/多核 100% 占用、预览卡死、导出停摆、笔记本过热；如果渲染与 UI 同线程等待，整个应用无响应。
- 计划覆盖：部分。§1.4 有“超时默认 1 秒”，§1.7 有崩溃处理。
- 缺口：超时只覆盖“等 rendered”，没有 CPU 累计预算和多进程看门狗；`Runtime.evaluate` 本身可能卡在渲染进程；Worker 未被禁；`requestAnimationFrame` 自循环未被禁。
- 缓解：
  1. CSP `worker-src 'none'` 禁 Worker/SharedWorker/ServiceWorker；`frame-src 'none'` 禁 iframe；组件契约明确禁止 `requestAnimationFrame` 自循环，宿主注入的 rAF 只允许宿主推进。
  2. 每个 CDP 调用带 deadline：`Runtime.evaluate` 使用协议 `timeout`（1000ms）+ 宿主定时器；超时先 `Runtime.terminateExecution`，1 秒无响应再 `Target.closeTarget`，第三次直接重启浏览器。
  3. 独立看门狗：每 500ms 读渲染进程 CPU 时间（Linux `/proc/<pid>/stat`，Windows `GetProcessTimes`，macOS `proc_pidinfo`）；1 秒墙钟内 CPU 增长 > 2 秒（多核）即杀 target。
  4. `--js-flags=--max-old-space-size=256 --max-semi-space-size=16`；`--renderer-process-limit=4` 限制进程数；不要用 `--single-process`。
  5. 失败策略：同一层连续 3 次超时/崩溃，标记该层“HTML 渲染已禁用”，需要人类在设置里重新启用；导出遇到超时立即失败，不写占位帧。
- 验收：`--html-timeout-test` 用死循环组件，2 秒内返回占位、UI 心跳 < 100ms、Chrome 进程被杀；连续 3 次后该层被禁用；Worker 构造抛异常。

#### H5 内存炸弹与 Canvas 超大分配

- 触发：`new Array(1e9).fill(0)`、拼接 GB 字符串、`canvas.width=30000`、`OffscreenCanvas`、大量 DOM 节点、`ImageData` 大分配。
- 影响：渲染进程 OOM，Chrome 崩溃；Linux OOM killer 可能杀掉主进程；导出中断；磁盘 swap 抖动。
- 计划覆盖：部分。只有 2MB HTML/5MB 资产/4096 边长，均不限制运行时内存。
- 缺口：没有 V8 堆上限以外的外部内存限制；没有 OS 级内存上限；没有 Canvas 尺寸守卫；没有进程 RSS 看门狗。
- 缓解：
  1. V8 堆上限 256MB；宿主每 250ms 采样浏览器进程树 RSS，超过 1.5GB 总预算或单 renderer 512MB 即杀 target/重启。
  2. 注入 Canvas 守卫：在 document-start 重定义 `HTMLCanvasElement.prototype.width/height` 的 setter 与 `OffscreenCanvas` 构造，超过 4096 或总像素 16MP 直接抛错；非可配置，防止页面覆盖。
  3. 截图只抓视口：`Page.captureScreenshot({clip:{x:0,y:0,width:W,height:H,scale:1}, captureBeyondViewport:false, fromSurface:true})`；禁止整页/超长截图。
  4. 像素预算：预览所有层合计 ≤ 4MP；导出合计 ≤ 33MP（4×4K）；超过则拒绝导出并提示降低层数/分辨率。宿主同时最多 2 个在途截图。
  5. Windows 用 Job Object 设 `JOB_OBJECT_LIMIT_PROCESS_MEMORY`；Linux 优先用 cgroup v2 `memory.max`（若可用）或 RSS 看门狗。**不要**把 `RLIMIT_AS` 设成 1.5GB：V8 会保留数 GB 虚拟地址空间，过低会直接启动失败；若必须用，设 8GB 以上并只作为兜底。
- 验收：`--html-memory-test` 分配 1GB，5 秒内浏览器被杀、主进程存活、该层禁用；4 层 4K（不是 4×4096²）导出时峰值 RSS 在预算内。

#### H6 字体/图片/SVG/CSS 资源耗尽

- 触发：WOFF2 brotli 解压膨胀、SVG `feGaussianBlur stdDeviation="10000"`、`viewBox="0 0 1e9 1e9"`、十万层 `box-shadow`、`filter: blur(2000px)`、`backdrop-filter`、超大渐变、`will-change`。
- 影响：单帧截图数秒到数十秒；GPU/软件光栅内存暴涨；导出逐帧超时；看起来像“CDP 卡死”。
- 计划覆盖：部分。§1.4 有 1 秒超时，§1.7 有资产大小限制。
- 缺口：5MB 资产不限制解码后大小；字体/SVG 滤镜在 Chrome 内解码，宿主无法预检；没有 DOM 节点数上限；没有“连续超时后禁用”的累计策略。
- 缓解：
  1. 资产导入时记录解码提示：图片最大 16384 边、64MP；字体解码后大小上限 64MB（导入时可让 Chrome 在 sandbox 里试渲染一次并测量，失败则拒绝）。SVG 优先作为 `<img src="data:...">` 使用（静态模式，不执行脚本）；内联 SVG 不假设能可靠清洗，靠 CSP + sandbox + 超时/内存看门狗兜底，并禁止外部引用。
  2. 每帧预算：加载 5 秒、渲染 1 秒、截图 3 秒；同一层连续 2 次超时即占位并降级，连续 3 次禁用。
  3. DOM 守卫：加载后和每次截图前在隔离世界执行 `document.getElementsByTagName('*').length`，> 50,000 判失败；`MutationObserver` 只做采样，不阻塞主流程。
  4. 禁用 CSS 动画：CDP `Animation.enable` + `Animation.setPlaybackRate(0)`，并注入 `*{animation:none!important;transition:none!important}`；不要只靠样式表（页面可后注入更高优先级规则）。
  5. 截图稳定性参数：`--force-color-profile=srgb`、`--font-render-hinting=none`、`--disable-font-subpixel-positioning`、`--disable-lcd-text`。
- 验收：`--html-resource-test` 用 SVG 大滤镜与 100k 节点页面，单帧在 5 秒内失败并占位；连续失败后层被禁用；导出不产生黑帧/错帧。

#### H7 父页面、DevTools 与跨层访问

- 触发：`window.parent.pvf`、`window.top.document`、`window.open('devtools://...')`、`fetch('http://127.0.0.1:9222/json')`、`chrome://` 导航；或宿主把多个层放在同一个未 sandbox 的文档里。
- 影响：读取/篡改宿主状态、其他层内容、工程数据；通过 DevTools 端口控制浏览器；跨层像素读取。
- 计划覆盖：部分。§1.6 写“每层一个 page”，但没有 iframe 沙箱和 DevTools 暴露面的要求。
- 缺口：如果直接用 `Page.setDocumentContent` 把组件放进主 frame，组件与宿主同源；如果用 blob URL 不加 sandbox，同样同源；CDP 端口暴露时任何页面都能访问 `/json`。
- 缓解：
  1. 宿主 shell 是固定可信文档；组件只能在 `sandbox="allow-scripts"` 的 iframe 中，且**不加** `allow-same-origin`。这样 `window.parent` 访问抛 `SecurityError`，`localStorage` 不可用，origin 为 opaque（`null`）。
  2. 宿主与组件只用 `postMessage` 通信：宿主校验 `event.source === iframe.contentWindow` 且 `event.origin === 'null'`，消息 schema 白名单，单条 ≤ 256KB；不把宿主对象暴露到组件世界。
  3. CDP 只用 `--remote-debugging-pipe`（见 B4）；任何情况下不监听 9222/9229 等固定端口。
  4. 每个层独立 iframe；截图只截宿主视口，不把其他层 DOM 合进同一文档。
- 验收：测试页尝试 `window.parent.document`、`window.top.pvf` 必须抛异常；尝试 `fetch('http://127.0.0.1:9222/json')` 必须失败；`ss -ltn` 无 Chrome 监听端口。

#### H8 Canvas 像素读取与工程数据暴露

- 触发：组件用 `canvas.toDataURL()`/`getImageData()` 读取自己的画布；若宿主把其他层/工程缩略图/未裁剪原图放进同一 canvas，则可读走。
- 影响：跨层/跨场景画面泄露；如果 `pvf.vars` 含其他场景的值，可推断工程内容。
- 计划覆盖：无。
- 缺口：没有定义 `pvf.vars` 和 `pvf.asset()` 的作用域；没有禁止宿主把多图层合成到组件 canvas。
- 缓解：
  1. `pvf.vars` 只含当前层 manifest 声明的变量在当前帧的值；不含实体 id、场景名、文件路径、注释。
  2. `pvf.asset(name)` 只解析当前层 manifest 的 `assets` 白名单，返回 data URL；不允许列目录、不允许读其他层资产。
  3. 宿主合成在 Chrome 之外用 QImage 完成；组件 canvas 只画自己。
  4. 组件读取自己像素是允许的，不视为漏洞；文档明确“HTML 内不要放秘密”。
- 验收：测试组件尝试 `pvf.asset('other-layer')` 返回 undefined；`pvf.vars` 不包含未声明键；跨层画面不出现在组件截图里。

#### H9 伪造 `pvf.rendered()` 与加载时序

- 触发：组件在初始化时立刻调用 `pvf.rendered()` 但图片/字体尚未 decode；或永不调用；或覆盖 `document.fonts.ready`、`requestAnimationFrame`。
- 影响：截到旧帧/空白帧，导出出现闪烁；确定性测试可能偶然通过（两次都截到同一旧帧）。
- 计划覆盖：部分。§1.5 写“等 `pvf.rendered()` + 两次 rAF”。
- 缺口：只信页面回调；没有图片 decode 等待；没有隔离世界；没有超时后的失败语义（预览占位、导出报错是有的，但触发条件不完整）。
- 缓解：
  1. 宿主等待逻辑放在 CDP 隔离世界（`Page.createIsolatedWorld` + `Runtime.evaluate(contextId=...)`），页面脚本无法 monkey-patch 隔离世界的 `requestAnimationFrame`/`document.fonts`。
  2. 等待顺序：`pvf.rendered()`（带 1s deadline）→ `document.fonts.ready`（隔离世界）→ `Promise.all([...document.images].map(i=>i.decode()))`（带 3s deadline）→ 两次原生 rAF（隔离世界）→ 截图。
  3. 组件契约要求 `pvf.rendered()` 必须在 `pvf.render()` 的同一任务末尾调用；宿主不因单次调用就立即截图。
  4. 预览超时显示占位并记录；导出超时立即失败并删除 `.part.mp4`，不写占位帧。
- 验收：`--html-late-image-test` 用延迟 decode 的图片，截图必须是最终图；`--html-lie-test` 组件提前调用 rendered，宿主仍等到图片 decode 完成；超时导出无输出文件。

#### H10 持久化、弹窗、自动重载与权限滥用

- 触发：`localStorage`、IndexedDB、Cache API、Service Worker、`window.open`、`location.reload()`、`beforeunload` 阻塞关闭、`navigator.clipboard`、`Notification.requestPermission()`。
- 影响：跨帧/跨工程残留状态，破坏确定性；弹窗/下载/通知骚扰；reload 循环拖死渲染；权限弹窗在 headless 下可能挂起。
- 计划覆盖：无。
- 缺口：没有存储隔离、弹窗/下载/通知策略、reload 次数限制。
- 缓解：
  1. sandbox iframe 无 `allow-same-origin`，存储为 opaque origin，文档销毁即失效；每次启动用全新 `--user-data-dir`，退出删除。
  2. `Browser.setPermission` 全部 deny；`--disable-notifications`；`--autoplay-policy=user-gesture-required`；`--mute-audio`。
  3. `Page.setDownloadBehavior('deny')`；sandbox 不加 `allow-downloads`/`allow-popups`/`allow-modals`。
  4. 监控 `Page.frameNavigated` 与 `Page.javascriptDialogOpening`：组件 frame reload > 3 次或出现 dialog，立即关闭 target；`beforeunload` 不阻止 `Target.closeTarget`。
- 验收：组件尝试弹窗/下载/通知/reload 循环，宿主记录 0 次成功，3 次 reload 后 target 被关闭。

### 2.2 浏览器二进制与路径（B）

#### B1 用户设置指向恶意二进制

- 触发：用户在设置里选择 `/tmp/chrome`、`~/Downloads/chrome`；或社会工程让用户“换成这个更快的内核”。
- 影响：点击“测试”或渲染 HTML 时以当前用户身份执行任意代码，绕过所有沙箱。
- 计划覆盖：部分。§1.1 有“手动路径框 + 浏览 + 测试”。
- 缺口：没有路径/所有权/权限/签名校验，没有二次确认，没有“路径被替换”检测。
- 缓解：
  1. 只接受绝对路径；`QFileInfo::canonicalFilePath()` 后必须是普通文件、可执行、非符号链接（或符号链接解析后仍满足）。
  2. Unix：owner 为 root 或当前用户，且 group/other 不可写（`mode & 022 == 0`）；Windows：`WinVerifyTrust` 校验 Authenticode（Google/Microsoft 签名）；macOS：`SecStaticCodeCheckValidity` + 期望 Team ID。
  3. 启动前 `--version` 探测（3 秒超时），输出必须匹配 `Google Chrome|Chromium|Microsoft Edge|Brave`；非标准路径显示红色警告并要求勾选“我信任此二进制”。
  4. 记录批准时的规范路径 + 文件大小 + mtime + SHA-256；下次启动若变化，重新提示（Chrome 自动更新会变，提示而不是静默拒绝）。
  5. 应用永不自动下载/解压浏览器；不从工程文件、MCP 或环境变量读取浏览器路径。
- 验收：`--settings-test` 选择 `/bin/sh` 必须被拒绝；选择 world-writable 的假 chrome 必须被拒绝；替换已批准二进制后启动出现重新确认。

#### B2 PATH 劫持与环境注入

- 触发：攻击者在 PATH 前部放 `google-chrome`/`chromium`；或继承 `LD_PRELOAD`、`DYLD_INSERT_LIBRARIES`、`http_proxy`、`CHROME_*` 环境变量。
- 影响：执行攻击者二进制；代理流量绕过断网；预加载库注入浏览器进程。
- 计划覆盖：无。
- 缺口：计划只说“自动探测常见路径”，没有说不用 PATH；现有 `CameraMonitor` 用 `QStandardPaths::findExecutable("ffmpeg")`，是同类反例。
- 缓解：
  1. 按平台固定绝对路径列表优先探测（Linux：`/usr/bin/google-chrome-stable`、`/usr/bin/chromium`、`/snap/bin/chromium`、`/opt/google/chrome/chrome`、Edge 对应路径；macOS/Windows 同理）。只有全部不存在时才用 `findExecutable`，并对结果做 B1 校验 + 日志显示解析到的真实路径。
  2. `QProcess::start(program, arguments)`，绝不经过 shell；参数用列表，不拼接字符串。
  3. `setChildProcessModifier`（Unix）清空 `LD_PRELOAD`、`LD_LIBRARY_PATH`、`DYLD_*`、`http_proxy`、`https_proxy`、`all_proxy`、`no_proxy`、`CHROME_*`；保留最小 `PATH=/usr/bin:/bin` 与 `HOME`（Chrome 可能需要）。
  4. Windows 使用 `CreateProcess` 的显式环境块；不要继承 `%PATH%` 中的用户目录给浏览器 helper。
- 验收：`PATH=/tmp/evil:$PATH` 启动，实际执行路径仍是系统 Chrome；`LD_PRELOAD` 指向的库未被加载（检查 `/proc/<pid>/maps`）；代理环境变量不出现在子进程 environ。

#### B3 Chrome 更新/参数漂移导致防护 fail-open

- 触发：Chrome 新版本移除/重命名 `--headless=new`、`--disable-3d-apis` 等；未知参数被静默忽略；企业策略覆盖。
- 影响：看起来启动成功，实际上网络/WebGL/file 访问全部打开；没有报错，最危险。
- 计划覆盖：部分。§6 只写“只用稳定域，同时支持 Edge”。
- 缺口：没有启动时行为验证；没有最低版本；没有“验证失败则禁用”的规则。
- 缓解：
  1. 每次启动跑 canary 自检（H1/H2/H7 的探针），验证的是**行为**而不是参数是否被接受；任一失败 → 禁用 HTML 渲染并显示具体失败项。
  2. 记录 `Browser.getVersion`；最低版本（例如 Chrome/Edge ≥ 120），低于则警告/禁用；不要硬编码过窄的版本区间。
  3. 参数分“安全关键”和“性能可选”：安全关键项的行为必须由 canary 证明；可选参数失败只降级性能。
  4. Edge/Chromium 分别跑同一套 canary；不能因为主浏览器通过就假设 Edge 通过。
- 验收：`--html-canary-test` 对每个探测项输出 PASS/FAIL；人为传一个无效的 `--disable-3d-apis` 别名，canary 必须 FAIL 而不是静默通过。

#### B4 remote-debugging 端口暴露与 CDP 未授权

- 触发：用 `--remote-debugging-port=9222` 或 `=0`；任何本机进程（包括其他用户）访问 `http://127.0.0.1:<port>/json`，拿到 `webSocketDebuggerUrl` 后完全控制浏览器。
- 影响：读取当前页面截图（工程画面）、执行任意 JS、导航到 `file://` 读本地文件、创建新 target 联网；完全绕过 MCP 权限和 HTML 沙箱。
- 计划覆盖：无。计划只写“CDP”。
- 缺口：没有指定 pipe；没有端口/绑定检查；没有 `--remote-allow-origins` 禁令。
- 缓解：
  1. **P0：使用 `--remote-debugging-pipe`**，通过 fd 3/4（Unix）或继承句柄（Windows）通信；不监听任何 TCP 端口。Qt 侧用 `QProcess::setChildProcessModifier` 把管道 dup2 到 3/4。
  2. 若平台/版本确实不支持 pipe：只允许 `--remote-debugging-port=0`，Chrome 默认绑 127.0.0.1；user-data-dir 0700；启动后读 `DevToolsActivePort`，立即删除文件；仍然接受“同用户其他进程可连”的残余风险，并在设置里标注。
  3. 永不传 `--remote-debugging-address=0.0.0.0`、`--remote-allow-origins=*`、固定端口。
  4. 启动后检查：`ss -ltnp`/`Get-NetTCPConnection` 中属于本浏览器进程树的监听端口集合必须为空（pipe 模式）；测试连接 9222/9229 必须拒绝。
- 验收：`--m6-security-test` 断言命令行含 `--remote-debugging-pipe` 且不含 `--remote-debugging-port`；`ss -ltn` 前后无新增 Chrome 端口；从另一个进程访问 9222 失败。

#### B5 CDP WebSocket 被本机进程连接

- 触发：B4 的端口模式；或应用自己实现了一个本地 WS 代理供调试，未加认证。
- 影响：同 B4；另外恶意进程可注入假截图、伪造渲染结果、读取 `pvf.vars`。
- 计划覆盖：无。
- 缺口：CDP 协议本身无认证；loopback 不等于同用户。
- 缓解：同 B4 的 pipe；如果必须用 WS，随机路径 + 一次性 token 只能防“不知道端口的进程”，不能防本机扫描；因此不作为主方案。pipe 的 fd 只被父子进程持有，其他进程无法连接。
- 验收：启动后 1 秒内扫描本机所有 Chrome 相关监听端口为空；pipe fd 不出现在 `/proc/<other>/fd`。

#### B6 浏览器用户数据目录与配置文件泄露

- 触发：误用用户默认 Chrome profile；或 user-data-dir 权限 0755，其他用户可读 Cookie/History；或页面通过 `--user-data-dir` 路径推断用户名。
- 影响：泄露用户浏览器登录态；页面可能带着用户 Cookie 访问网络（如果断网失败）；污染用户配置。
- 计划覆盖：部分。§1.1 写“独立用户数据目录”。
- 缺口：没有权限、位置、清理、与默认 profile 的断言。
- 缓解：
  1. 目录固定在 `~/.cache/PVForge/chrome/<uuid>/`，创建时 0700；启动前断言其规范化路径不等于任何已知默认 profile 路径。
  2. 每次会话新建；退出/崩溃后清理；启动时清理 7 天前的残留（只删带 PVForge 标记文件的目录）。
  3. `--disable-sync --disable-extensions --disable-default-apps`；不复制任何用户数据。
- 验收：目录 mode 0700；`--user-data-dir` 不含用户名默认路径；退出后目录被删除或标记为待清理。

#### B7 root、fd 继承与进程加固

- 触发：`sudo pvforge`；父进程打开的工程文件/MCP socket/token 文件 fd 被 Chrome 继承；core dump 包含页面内存。
- 影响：Chrome 以 root 运行（沙箱失效）；Chrome 可读写不该碰的 fd；崩溃转储泄露工程数据。
- 计划覆盖：无。
- 缺口：没有 fd 关闭、rlimit、root 检查。
- 缓解：同 H3；`setChildProcessModifier` 中先把 CDP 管道 dup2 到 fd 3/4，再关闭 4 以上的所有 fd，`setrlimit(RLIMIT_CORE,0)`、`setsid()`、`umask(077)`；`geteuid()==0` 拒绝 HTML；启动时可选 `prctl(PR_SET_DUMPABLE,0)` 降低主进程 core。
- 验收：`ls -l /proc/<chrome>/fd` 只看到 stdio + 两个 CDP 管道 + 必要的 Chrome 内部 fd；`ulimit -c` 为 0；root 下 HTML 按钮禁用。

### 2.3 恶意 MCP 客户端 / AI（M）

#### M1 token 泄露（文件、日志、argv、查询串）

- 触发：endpoint 文件 0644；日志打印 `Authorization`；token 放在命令行/URL query；崩溃转储；桥接进程把 token 打到 stderr。
- 影响：任何拿到 token 的本机用户/进程可读写工程（在权限模式下）。
- 计划覆盖：部分。§2.1 写“随机 Bearer token”“mcp-endpoint.json（0600）”。
- 缺口：0600 不够（同用户可读）；没有轮换、日志脱敏、argv/query 禁令；`~/.config` 可能被同步/备份。
- 缓解：
  1. 32 字节 CSPRNG（`QRandomGenerator::system()`）→ base64url 43 字符；只在内存和 endpoint 文件中存在，不落盘到日志/设置。
  2. endpoint 放 `QStandardPaths::RuntimeLocation`（Linux `/run/user/<uid>/pvforge/`，0700），而不是 `~/.config`；目录 0700；先在目录内用 `O_CREAT|O_EXCL|O_NOFOLLOW,0600` 写临时文件，`fsync` 后 `rename` 到最终路径，再 `fsync(dir)`；启动时先检查残留文件的 PID 是否存活，不盲目覆盖。
  3. 只接受 `Authorization: Bearer <token>`；拒绝 query/`X-Api-Key` 等变体；比较用常量时间（固定长度字节逐位 XOR）。
  4. 日志只记 token 前 4 位 + 长度；HTTP 层不打印 header；桥接进程不打印 token。
  5. 每次启用/重置/权限变更都轮换 token，旧 token 立即失效；停止时删除 endpoint；崩溃残留由桥接进程检查 PID 后忽略。
  6. 主进程 `RLIMIT_CORE=0`，或至少确保 core 模式 0600；错误报告不附环境变量。
- 验收：`stat` 文件 0600/目录 0700；`grep -R <token>` 在日志/配置/argv 中为 0；旧 token 在轮换后 401；`ps` 看不到 token。

#### M2 非 loopback 绑定

- 触发：复制现有 `CameraMonitor` 的 `QHostAddress::Any` 写法；或为了“手机也能连”提供 0.0.0.0 选项。
- 影响：局域网任何人可访问 MCP；配合 token 泄露就是远程写工程；即使拒绝，也会暴露端口、可被 DoS。
- 计划覆盖：部分。§2.1 写“监听 127.0.0.1”。
- 缺口：没有强制代码/测试；现有代码已有 `Any` 反例（`src/ui/CameraMonitor.cpp:987`）；IPv6 `::` 也可能被误用。
- 缓解：
  1. 只 `listen(QHostAddress::LocalHost, port)`；不提供“监听所有接口”设置。若需 IPv6，单独 bind `::1`，不要 `Any`。
  2. 每个连接 `peerAddress().isLoopback()` 复核；非 loopback 直接 abort（防止未来改 bind 地址）。
  3. 端口冲突时明确失败，不扫描 8080–8089 后落到其他接口；端口可配置但必须 1024–65535，且只绑 loopback。
  4. 文档明确：远程使用必须 SSH 隧道/Unix socket，v1 不支持 LAN。
- 验收：从同网段另一台机器 `curl http://<host>:<port>/mcp` 连接被拒绝；`ss -ltn` 显示 `127.0.0.1:<port>` 而非 `0.0.0.0`；IPv6 未意外监听。

#### M3 DNS rebinding、浏览器 CSRF 与 CORS

- 触发：用户浏览器打开恶意网页，JS 向 `http://127.0.0.1:<port>/mcp` 发请求；攻击者域名解析到 127.0.0.1（DNS rebinding）；或 `Origin: https://evil.com`。
- 影响：若 token 缺失/泄露，可代用户调用工具；即使有 token，也可探测端口、发垃圾请求。
- 计划覆盖：无。§2.1 只写 loopback + token。
- 缺口：没有 Host/Origin 校验；没有禁止 CORS；没有 `Content-Type` 强制。
- 缓解：
  1. 校验 `Host` 必须是 `127.0.0.1:<port>`、`localhost:<port>` 或 `[::1]:<port>`；其他一律 403。这是 DNS rebinding 的关键。
  2. `Origin` 存在时必须为 `null` 或与 Host 完全匹配；来自 `https://...` 的一律 403。检查 `Sec-Fetch-Site`，`cross-site` 拒绝。
  3. 不返回任何 `Access-Control-Allow-*` 头；POST 强制 `Content-Type: application/json`（拒绝 `text/plain`，避免 simple request）；要求自定义头 `X-PVForge-Client: 1`，迫使浏览器预检，而预检不会通过。
  4. token 仍是主防线；Host/Origin 是纵深。
- 验收：`curl -H 'Host: evil.com'` → 403；`Origin: https://evil.com` → 403；浏览器 fetch 因 CORS 失败；正常 `127.0.0.1` + token → 200。

#### M4 HTTP 解析与请求走私

- 触发：同时发 `Content-Length` 与 `Transfer-Encoding: chunked`；重复/负数/溢出的 Content-Length；超长 header；NUL/折行；pipelining 两个请求。
- 影响：绕过 body 限制、认证或限流；请求边界错乱，第二个请求被当成第一个的 body；若前面有代理（SSH 隧道、反向代理）可造成走私。
- 计划覆盖：部分。§2.1 写“JSON-RPC、拒绝超大请求”。
- 缺口：没有解析器规则、没有 TE 禁令、没有 header 上限、没有 keep-alive 策略。
- 缓解：
  1. 只支持 HTTP/1.1，不使用 h2c；拒绝 `Transfer-Encoding`（任何值）；只接受单个 `Content-Length`，十进制、≤ 8 MiB、无前导 `+`/空格。
  2. header 总大小 ≤ 16 KiB，请求行 ≤ 8 KiB；重复的 `Host`/`Content-Length`/`Authorization` 一律 400；header 名只允许 token 字符。
  3. 每个连接同时只处理 1 个请求；不支持 pipelining；解析失败立即 `Connection: close`。
  4. 优先用 Qt 的 `QHttpServer`（若 Qt 版本可用）并只在其上做限制；若自写 `QTcpServer`，必须有独立的请求解析器和 fuzz 测试语料（CL+TE、双 CL、超长、慢速）。
  5. 路径只允许 `POST /mcp`（和可选 `GET /mcp` SSE）；其他 404；方法不允许 405。
- 验收：`--mcp-http-test` 覆盖上述畸形请求，全部 400/关闭连接且不影响后续正常请求；8 MiB+1 body → 413；慢 header 5 秒关闭。

#### M5 超大 body、JSON 深度与慢连接 DoS

- 触发：100MB body；100k 层嵌套数组；1000 个空闲连接；每 4 秒发 1 字节 header（slowloris）。
- 影响：内存/CPU/fd 耗尽；UI 线程被拖死；正常 AI 调用超时。
- 计划覆盖：部分。§2.3 有单 blob 5MB、单批 200 操作，但没有传输层限额。
- 缺口：没有 header/body/连接/超时/速率上限。
- 缓解：
  1. body ≤ 8 MiB（5 MiB blob 的 base64 约 6.7 MiB + JSON 开销）；header ≤ 16 KiB；JSON 深度 ≤ 64；单字符串 ≤ 1 MiB；JSON-RPC batch 数组拒绝或 ≤ 20 项。
  2. 连接 ≤ 16，每连接 header 5s、body 15s、总请求 30s、空闲 30s；超过即关闭；accept 速率 ≤ 20/s。
  3. 同时处理的请求 ≤ 4，写队列 ≤ 32；队列满返回 429/`busy`，不无限堆积。
  4. 网络 I/O 在独立线程/事件循环；UI 线程只做项目事务；禁止从网络线程 `BlockingQueuedConnection` 到可能正在等待网络的 UI。
- 验收：`--mcp-dos-test` 100 个连接被限制在 16；慢 header 5s 关闭；8MiB body 413；UI 心跳在攻击期间 < 200ms。

#### M6 工具参数注入（路径、id、数字、字符串、HTML/manifest）

- 触发：`assets.import_blob` 的 `filename` 含 `../../`；`html.create_layer` 的 `assetId` 含 `../` 或非 64 hex；`keyframes.set` 的 frame 为 `1e309`/NaN；`entity.update` 的 `data` 注入 `sourcePath`、`script`、`url`；entity name 含控制字符。
- 影响：路径穿越读写；NaN/Inf 破坏插值和序列化；通过 data 注入让宿主加载任意文件；日志/UI 注入（见 M10）。
- 计划覆盖：部分。§2.2 写“无路径”“base64/blob + 元数据”。
- 缺口：没有逐工具 schema、范围、正则、有限性校验；没有 data 键白名单。
- 缓解：
  1. 每个工具用严格 JSON Schema（`additionalProperties:false`），执行前统一校验；未知字段拒绝。
  2. 路径类：v1 写工具**不接受任何路径**；`assets.import_blob` 只收 `base64`、`mime`、`displayName`；文件名只用于显示，禁止 `/`、`\`、`..`、NUL、Windows 保留名（`CON`、`NUL`、`COM1` 等），长度 ≤ 128。
  3. `assetId` 必须 `^[0-9a-f]{64}$` 且文件存在、哈希复核通过；`entityId` 必须为正整数且存在；`sceneId` 同理。
  4. 数字：`std::isfinite`；frame ∈ [0, 1_000_000]；value ∈ [-1e9, 1e9]；easing 控制点 ∈ [-10, 10]；整数字段拒绝小数。
  5. `entity.update` 的 `data` 按实体类型白名单键；禁止 `sourcePath`、`path`、`url`、`script`、`command`、`file`、`audioPath`；字符串长度 ≤ 4 KiB（名称 ≤ 256）。
  6. HTML manifest：组件 ≤ 16，变量总数 ≤ 256，变量名 `^[A-Za-z_][A-Za-z0-9_]{0,63}$`，min/max/default 有限且 min ≤ default ≤ max，尺寸 16–4096，重复 id/name 拒绝。
- 验收：`--mcp-input-test` 对每个工具跑恶意参数集，全部返回 `invalid_argument` 且工程 JSON 哈希不变；`filename: "../../x"`、`assetId: "../x"`、`frame: 1e309` 均被拒。

#### M7 批量操作 DoS 与工程膨胀

- 触发：AI 以 2 次/秒循环 `batch.run`，每批 200 个 `entity.create`；或创建 100 万个关键帧；或反复 `scene.create` 追加空场景。
- 影响：工程实体/关键帧爆炸，UI 卡死，checkpoint 磁盘暴涨，导出不可用；人类无法撤销。
- 计划覆盖：部分。§2.3 有单批 200、实体 5000；§1.7 有 HTML 限额。
- 缺口：没有请求速率、每会话总量、每批新建数/关键帧数、最小批次间隔、工程尺寸上限。
- 缓解：
  1. 速率：写请求 2/s、批次 ≥ 500ms 间隔、每会话 10 分钟最多 100 批；超限 `rate_limited`。
  2. 每批 ≤ 200 ops、新建实体 ≤ 50、新建关键帧 ≤ 2000；工程 ≤ 5000 实体、≤ 200k 关键帧、≤ 20k 轨道；在事务内检查，避免 TOCTOU。
  3. `scene.create` 每会话 ≤ 10，场景 ≤ 1000；新场景长度 ≥ 1 帧且 ≤ 1e6。
  4. checkpoint 最小间隔 10 秒（同一批合并），保留 100 个且总大小 ≤ 2 GiB；磁盘剩余 < 5 GiB 时拒绝写操作。
  5. 连续失败 3 次或 10 分钟内 10 次错误 → 自动解除武装并提示人类。
- 验收：`--mcp-quota-test` 第 201 个 op 被拒且工程不变；实体到 5000 后再创建被拒；100 批后 checkpoint 数量/大小在预算内。

#### M8 越权写其他场景与全局对象

- 触发：AI 拿到其他场景的 entity id 直接 `entity.update`；把实体 `move` 到其他场景的文件夹；写 `activeCamera`、音频、全局台标；人类在 AI 操作期间拖动播放头到另一个场景；`scene.create` 创建新场景后继续写。
- 影响：绕过“只写当前场景”，改坏其他段落；全局对象影响整个工程。
- 计划覆盖：部分。§2.3 写“当前播放头所在场景的根文件夹；全局对象需显式解锁”。
- 缺口：没有执行层算法；没有处理“播放头移动”“场景空隙”“场景根祖先”“scene.create 扩权”；只读工具仍能读全工程（见 M11）。
- 缓解：
  1. 写入范围在**人类解锁/批次开始时快照**：`{projectUuid, sceneId, sceneRootId, frameRange}`；人类移动播放头不改变本次 AI 的 scope；若人类切场景，自动解除武装并提示。
  2. 每个目标实体沿 `parent` 链找到顶层祖先，必须等于 `sceneRootId`；目标本身是场景根文件夹时只允许受控的 rename/退休，不允许 move/delete。
  3. 播放头落在场景空隙（不属于任何场景）时，写工具返回 `no_active_scene`，不回退到全局根。
  4. `entity.move`/`folder.create` 只能在同一场景根内；禁止把实体移出场景根；禁止把其他场景实体移入。
  5. 全局对象（activeCamera、全局音频、台标）单独工具 + 人类确认；v1 建议直接不提供，避免“解锁一次永久生效”。
  6. `scene.create` 只能追加到时间轴末尾，且创建后不自动成为 AI 可写范围；需要人类确认或至少 checkpoint + 明确返回新 scope。
- 验收：`--mcp-scope-test` 在场景 A 激活时改场景 B 实体 → `permission_denied`；播放头移到空隙 → 写拒绝；move 出根 → 拒绝；activeCamera 无解锁 → 拒绝。

#### M9 伪造 stopped/权限、重连绕过与取消滥用

- 触发：客户端重新 `initialize`；伪造 `notifications/cancelled` 取消别人的请求；在只读模式下直接 `tools/call` 写工具；急停后立刻重连。
- 影响：绕过急停/只读；取消他人的长任务；审计日志出现“已停止但仍在写”。
- 计划覆盖：部分。§2.4 有“服务端进入已急停”，但没有状态机/重连语义。
- 缺口：没有定义 `initialize` 是否重置状态；没有 session 绑定；没有取消归属校验。
- 缓解：
  1. 服务端状态机：`Disabled → ReadOnly → ArmedWrite(scope, expires) → Stopped`；只有人类 UI 能触发 `→ ArmedWrite` 和 `Stopped → ReadOnly`；`initialize` 只返回当前状态，不改变状态。
  2. 每个请求绑定 `sessionId`（服务端生成 32 hex）；`tools/call` 执行时再查权限，不能只依赖 `tools/list` 隐藏。
  3. `notifications/cancelled` 只能取消同 session 且未提交的请求；request id 映射到 session，跨 session 返回 `permission_denied`。
  4. 急停状态在重连后保持；权限带过期时间（写权限 30 分钟无操作自动回只读）；项目切换/关闭立即降为只读。
  5. 所有写工具在 `Stopped` 下返回 `stopped_by_human`（不可重试），读工具仍可用；日志记录状态转换。
- 验收：`--mcp-stop-test` 急停后重连再 `initialize`，写仍拒绝；只读模式下直接调用写工具 → `permission_denied`；跨 session cancel → 拒绝。

#### M10 日志注入与 UI 欺骗

- 触发：实体名/场景名/注释含 `\n[成功] 已删除...`、ANSI `\x1b[...]`、RTL 覆盖 `\u202E`、1MB 字符串；日志窗用富文本渲染。
- 影响：伪造审计记录、隐藏真实操作、UI 排版错乱；若用 `setHtml`，可执行 HTML/JS（Qt 富文本子集）。
- 计划覆盖：部分。§2.5 有日志窗，但没有内容处理。
- 缺口：没有转义、截断、控制字符过滤；日志默认只存内存但可导出 JSONL。
- 缓解：
  1. 日志一律结构化记录；UI 用 `QPlainTextEdit` 或转义后的富文本；永不 `setHtml`。
  2. 所有用户/AI 字符串：去 NUL/C0/C1、去 bidi 控制字符（U+202A–U+202E、U+2066–U+2069）、单行化、截断 256 字符；显示时加引号。
  3. 参数日志：`assets.import_blob` 只记 hash/大小/MIME，不记 base64；`html.get` 只记大小/hash；不记 token/Authorization。
  4. JSONL 导出用标准 JSON 转义；文件 0600；轮转 10 MiB × 3。
  5. 日志条目上限 10,000 条 / 8 MiB；缩略图总预算 32 MiB，单张 ≤ 256×144。
- 验收：`--mcp-log-test` 注入换行/ANSI/RTL/1MB 名称，日志显示为单行且无样式变化；导出 JSONL 可被 `jq` 解析；内存不随 100k 条增长。

#### M11 preview 截图与只读工具的数据外传

- 触发：AI 调用 `preview.render_frame` 遍历所有帧/场景；`annotations.list`/`scene.context` 读全工程；AI 客户端把 PNG/文本发给远程模型。
- 影响：未发布歌词、分镜、客户素材泄露给第三方；即使“只读”也会传数据。
- 计划覆盖：部分。§2.2 有预览限流 2 次/秒；§2.3 只读默认。
- 缺口：没有数据范围同意；预览可读全工程；没有分辨率/响应大小上限；没有“AI 正在看”提示。
- 缓解：
  1. 启用 MCP 时明确告知“只读仍会把工程内容发送给 AI 提供方”，列出会发送的数据类型；首次连接需人类确认。
  2. `preview.render_frame` 默认只允许当前场景帧；跨场景需人类临时授权；单张长边 ≤ 1280，PNG ≤ 8 MiB；2 次/秒、并发 1。
  3. `annotations.list`/`scene.context` 按当前场景过滤；全工程读取需要单独“允许读取全工程”开关。
  4. 每次预览在视口/日志显示“AI 正在查看第 N 帧”；不提供批量导出/视频。
  5. 服务端可关闭 `preview.render_frame`（设置项），关闭后工具从列表消失且调用被拒。
- 验收：场景 A 激活时请求场景 B 帧 → `permission_denied`；1 秒内第 3 次预览 → 429；响应 > 8MiB 被截断/拒绝；关闭预览后调用失败。

#### M12 stdio 桥、端点文件冒充与会话劫持

- 触发：PATH 中有恶意 `pvforge-mcp`；endpoint 文件被替换成攻击者服务器；桥接进程记录 token；token 被同一用户的其他进程复用。
- 影响：AI 客户端连到假服务器（钓鱼/窃取工程内容）；或桥接进程把 token 泄露给第三方。
- 计划覆盖：部分。§2.1 写“stdio 桥配置说明”。
- 缺口：没有桥接程序完整性、endpoint 校验、token 处理规则。
- 缓解：
  1. 配置片段使用随应用安装的绝对路径，不用 PATH 名；桥接程序不读工程、不落盘 token、不打印 token。
  2. 桥接读取 endpoint 后校验：文件 owner == euid、mode 0600、目录 0700、JSON schema、`pid` 存活且进程启动时间匹配（Linux `/proc/<pid>/stat` 第 22 字段）、URL host 是 `127.0.0.1`/`[::1]`。
  3. 校验失败输出明确错误，不尝试其他端口/文件；不自动重试到 `localhost` 的其他服务。
  4. 服务端限制单 token 同时 1 个写 session；新 session 建立时旧 SSE 连接关闭。
- 验收：伪造 endpoint 指向 127.0.0.1:其他端口 → 桥拒绝；endpoint 权限 0644 → 桥拒绝；两个写客户端同时连接 → 第二个 409/`busy`。

### 2.4 本地攻击者（L）

#### L1 读取 endpoint/token

- 触发：其他用户读 `~/.config`；备份/同步工具上传；`ps`/`/proc` 泄露；日志。
- 影响：完整工程读写（在允许修改时）；至少可读取工程。
- 计划覆盖：部分（0600）。
- 缺口：位置在 config（可能同步）；同用户无法防；没有轮换/过期。
- 缓解：M1 的 runtime dir + 0600/0700 + 轮换 + 不落日志；同用户残余风险在文档中明示，提供“每次调用确认”模式（高风险工程）。
- 验收：其他用户 `cat` 失败；同步目录中不存在 endpoint；重启后旧 token 失效。

#### L2 读取/篡改 checkpoint 仓库（含 git hook 代码执行）

- 触发：checkpoint 目录 0755；攻击者写入 `hooks/pre-commit`（或非 bare 仓库的 `.git/hooks/pre-commit`）、`core.fsmonitor`、`GIT_CONFIG_GLOBAL`；或删除对象。
- 影响：读取完整工程历史；在应用执行 git 时以用户身份执行 hook；恢复被篡改的快照。
- 计划覆盖：部分。§2.7 只写位置和保留数。
- 缺口：没有权限、git 配置隔离、hook 禁用、完整性校验；git 仓库本身可被本地篡改。
- 缓解：
  1. 目录 0700、文件 0600；项目 UUID 子目录；不把工程路径写进文件名/index 明文字段。
  2. 只用 plumbing：`git init --bare`、`git hash-object -w --stdin`、`git mktree`、`git commit-tree`、`git update-ref`；不 `git add`/`commit`/`checkout`，不运行 hook。
  3. 环境：`GIT_CONFIG_NOSYSTEM=1`、`GIT_CONFIG_GLOBAL=/dev/null`、`-c core.hooksPath=/dev/null`、`-c protocol.file.allow=never`、`--no-optional-locks`；显式 `--git-dir`。
  4. 每个快照存 sidecar SHA-256；恢复时先校验 JSON 哈希；git 对象损坏/缺失则报“检查点损坏”，不静默重置。
  5. 若 git 不可用/版本过旧，回退到普通快照文件（同样 0600、同样 hash 校验）；不要把 git 作为唯一后端。
- 验收：`--mcp-checkpoint-test` 在 `.git/hooks/pre-commit` 放一个写文件的脚本，提交后该文件不存在；删除一个 object 后恢复报错；快照 hash 不符拒绝恢复。

#### L3 替换工程文件与自动保存覆盖

- 触发：外部进程/同步工具修改 `.pvproj`；用户在应用打开时用编辑器改；自动保存每 5 分钟无条件 `Project::save`。
- 影响：覆盖外部修改；把 AI 中间状态写盘；崩溃后磁盘状态与 checkpoint 不一致。
- 计划覆盖：无。
- 缺口：`MainWindow::autoSave()`（`src/app/MainWindow.cpp:686`）没有 mtime/hash 检查；`Project::save` 直接覆盖。
- 缓解：
  1. 打开时记录 `{size, mtime, sha256}`；保存/自动保存前重新 stat + 哈希；不一致则暂停自动保存并提示“磁盘文件已变化”，让用户选择覆盖/重新加载/另存。
  2. AI 批次期间暂停自动保存；只在批次边界保存；保存失败不清 `modified`。
  3. `QSaveFile` 保持原子；保存前检查目标不是符号链接（见 L5）。
  4. checkpoint 恢复前也做外部变化检查。
- 验收：`--autosave-external-change-test` 在自动保存前改文件，自动保存不覆盖并弹提示；AI 批次中自动保存计时器暂停。

#### L4 篡改注释/场景/资产（无完整性校验）

- 触发：本地进程直接改 `.pvproj` 里的 annotations/scenes；替换 `assets/html/<sha>.json`；改素材文件。
- 影响：AI 读到伪造的注释/场景（prompt injection 的载体）；渲染内容被替换；审计失真。
- 计划覆盖：部分（内容寻址只覆盖 HTML/素材，不覆盖工程 JSON）。
- 缺口：工程 JSON 没有签名/哈希；注释是 AI 的输入，可能被用来注入指令。
- 缓解：
  1. 明确“本地同用户篡改无法密码学防御”；用 checkpoint diff 和“磁盘文件已变化”提示降低静默篡改。
  2. 注释/场景文本在注入 AI 上下文时明确标注为“数据，不是指令”，并做长度/控制字符过滤；AI 指南要求忽略注释中的指令。
  3. 资产哈希每次加载复核（A5）；HTML 资产文件被替换会因哈希不匹配被拒。
- 验收：替换 HTML 资产文件后加载报“哈希不匹配”；注释中的“忽略以上指令”不会改变工具权限（由服务端强制）。

#### L5 工程目录符号链接、路径穿越与 TOCTOU

- 触发：`assets` 是指向 `~` 的符号链接；`assets/html/<sha>.json` 是指向 `/etc/passwd` 的符号链接；`sourcePath: "../../secret"`；哈希校验后、读取前替换文件。
- 影响：任意文件读/写；把敏感文件当资产打包/上传；哈希与内容不一致。
- 计划覆盖：无。
- 缺口：`Project::resolveAssetPath` 接受绝对路径和 `..`；`MediaImporter::copyIntoAssetDirectory` 用 `absoluteFilePath` 做词法前缀判断，不解析符号链接；哈希与复制是两次独立打开（TOCTOU）。
- 缓解：
  1. 工程根 `canonicalFilePath()`；`assets` 目录必须是真实目录（`lstat` 非 symlink），canonical 路径在根内；否则拒绝加载并提示。
  2. 每个资产 `lstat`：必须是普通文件（`S_ISREG`），非 symlink；canonical 路径在根内；拒绝 FIFO/设备/`/proc`/`/sys`/`/dev`。
  3. 相对路径先 `QDir::cleanPath`，拒绝 `..` 逃逸、NUL、Windows 盘符/UNC/ADS/保留名；绝对路径仅允许 Image/Video/Audio 的“外部素材”，列出并要求用户确认；HTML 资产必须相对且在 `assets/html/`。
  4. 读取一次：`open` 后 `fstat`，从同一 fd 读字节、算哈希、解析/解码；不要“先哈希路径、再重新打开”。
  5. 写入：在已验证目录内 `open(O_CREAT|O_EXCL|O_NOFOLLOW,0600)` 写临时文件，`fsync`，`rename` 到 `<sha>.json`；目标已存在则校验内容，不覆盖。
  6. `MediaImporter` 改为“边复制边哈希，临时文件命名为 hash，最后 rename”；解决 `sha256File` 后 `QFile::copy` 的 TOCTOU。
- 验收：`--asset-path-test` 覆盖 symlink assets、`../`、绝对路径 HTML、FIFO、哈希后替换源文件；全部拒绝或安全处理；复制后文件名哈希 == 内容哈希。

#### L6 不可信来源工程自动渲染

- 触发：用户从 Download/聊天/网盘打开含 HTML 层的 `.pvproj`；应用打开即启动 Chrome 并渲染。
- 影响：在用户还没意识到的情况下执行不可信 HTML；结合 H1/H3 可外传/逃逸。
- 计划覆盖：无。§1.1 只写“首次需要渲染时启动”。
- 缺口：没有“项目信任级别”；没有隔离标记（macOS quarantine/Windows MOTW）；没有只读打开。
- 缓解：
  1. 打开工程时检测来源：macOS `com.apple.quarantine` xattr；Windows `Zone.Identifier` ADS；路径在 `/tmp`、`/media`、`/run/media`、`/mnt`、Downloads。
  2. 不可信工程默认不启动 Chrome：HTML 层显示占位；顶部横幅“此工程包含未验证的 HTML 组件”；用户点“本次渲染”才启动；可“信任此工程”记录 projectId + 文件哈希。
  3. 不可信工程可只读打开（至少禁用 HTML 和外部资产读取），保存时提示另存。
  4. 首次打开列出外部绝对路径资产，逐项确认；HTML 资产必须在工程内。
- 验收：`--untrusted-project-test` 把带 HTML 的工程放到 `/tmp`，打开后 Chrome 未启动；点“本次渲染”后才启动；信任记录后再次打开直接渲染。

### 2.5 HTML 资产与工程文件（A）

#### A1 不可信 `.pvproj` 加载：大小/实体数/键数无上限

- 触发：64MB/1GB JSON；100 万实体；单轨道 100 万关键帧；超长字符串；深层嵌套。
- 影响：`Project::load()` 的 `file.readAll()` 一次分配整个文件，再构建 QJsonDocument（约 2–3 倍内存），UI 冻结/OOM；恶意文件可让应用打不开。
- 计划覆盖：部分。§4 只写版本和字段，没有加载限额。
- 缺口：现有 `Project::load`（`src/core/Project.cpp:1525`）没有文件大小、实体数、键数、字符串长度检查；版本号根本没读。
- 缓解：
  1. `QFileInfo::size()` 先检查：工程 ≤ 64 MiB，否则拒绝；流式读取并限制总量。
  2. 解析后逐项限额：实体 ≤ 5000、模板 ≤ 1000、每实体轨道 ≤ 128、每轨道关键帧 ≤ 10,000、全工程关键帧 ≤ 200,000、字符串 ≤ 1 MiB、名称 ≤ 256、描述 ≤ 64 KiB。
  3. 版本：只接受 1–4；`>4` 明确报错；`format != "pvforge"` 拒绝；未知实体类型拒绝或占位，**不要**默认成 Image 去加载 `sourcePath`。
  4. 重复 entity id 拒绝；id ≤ 0 拒绝；parent 环已有校验，保留并加深度上限（≤ 256）。
  5. 加载失败时不修改当前工程（先解析到临时 `Project` 再 swap）。
- 验收：`--project-limit-test` 65MiB、5001 实体、10,001 键、1MiB 字符串、version 5 全部被拒且应用不崩；正常 v1–v4 通过。

#### A2 `sourcePath`/`logos`/template 路径穿越与特殊文件

- 触发：`sourcePath: "/etc/shadow"`、`"../../secret"`、`"/dev/zero"`、`"//server/share/x"`、`logos` 里 1000 个路径。
- 影响：读取任意文件（图片/音频解码失败也可能泄露存在性/大小）；`/dev/zero` 导致无限读取；网络路径导致挂起；把敏感文件缩略图带进工程。
- 计划覆盖：无。
- 缺口：`resolveAssetPath` 对绝对路径原样返回；没有类型/大小/特殊文件检查；`logos` 数量未限。
- 缓解：
  1. 类型限制：HTML 资产禁止外部路径；Image/Video/Audio 允许绝对路径仅作为“外部素材”，加载前 `isFile()` + 大小 ≤ 256 MiB + 尺寸/时长上限；拒绝 FIFO/设备/网络路径。
  2. 相对路径限制在工程根内（canonical）；`..` 逃逸拒绝。
  3. `logos` ≤ 64，每项同图片规则。
  4. 不可信工程的外部路径逐项确认（L6）；UI 列出“此工程将读取以下外部文件”。
- 验收：`/dev/zero`、`/etc/shadow`（作为 image/audio）、`../../x` 全部拒绝或需确认；`logos` 65 个拒绝。

#### A3 图片/视频/音频解码与尺寸炸弹

- 触发：20000×20000 PNG（解压后 1.6GB）、动画 WebP 1000 帧、超长视频/音频、畸形 FFmpeg 容器。
- 影响：宿主进程 OOM；解码器漏洞（FFmpeg/Qt 图片插件）导致代码执行；加载工程卡死。
- 计划覆盖：部分。§1.7 只限制 HTML 资产大小；§1.2 说字体/图片内联。
- 缺口：没有图片尺寸/像素上限；`Project::load` 直接 `QImage(path)`；FFmpeg 解码无时长/分辨率上限。
- 缓解：
  1. 图片：`QImageReader` 先读尺寸，拒绝 > 16384 边或 > 64MP；设置 `setAllocationLimit(256MB)`；只取第一帧，拒绝动画（或只支持静态）。
  2. 视频：容器探测时长 ≤ 4 小时、分辨率 ≤ 8192×8192、像素 ≤ 33MP；只解第一帧用于缩略图；超限拒绝导入。
  3. 音频：时长 ≤ 4 小时、采样率 ≤ 192kHz、声道 ≤ 8；解码在 worker 线程，带取消。
  4. 更新 Qt/FFmpeg；不可信工程的缩略图可在 Chrome sandbox 里生成，减少宿主解码器暴露（P1）。
- 验收：`--media-bomb-test` 20000×20000 PNG、1000 帧 WebP、4h+ 视频全部拒绝且内存不暴涨。

#### A4 超大资产、字体/图片/SVG 解码膨胀（宿主侧）

- 触发：5MB WOFF2 解压到 500MB；SVG 内嵌巨大滤镜；HTML 里 data URL base64 膨胀；缩略图生成时一次性解码所有资产。
- 影响：宿主内存/CPU 峰值；打开素材库卡死。
- 计划覆盖：部分（5MB 资产、2MB HTML）。
- 缺口：没有解码后大小、缩略图并发、总资产预算。
- 缓解：资产导入时记录原始/解码大小；字体解码 > 64MB 拒绝；缩略图串行/最多 2 并发、单张 ≤ 512×512；素材库懒加载；总资产（工程内）≤ 2GB 警告。
- 验收：`--asset-decode-test` 记录峰值内存；超限资产拒绝并给错误。

#### A5 内容寻址哈希未校验/TOCTOU/命名冲突

- 触发：把任意文件命名为 `<sha>.json`；哈希后替换源文件；同名资产内容不同；大小写/编码差异。
- 影响：加载到与 id 不符的内容；checkpoint 恢复后素材被替换；审计失真。
- 计划覆盖：部分。§1.2 写“内容寻址 JSON：assets/html/<sha>.json”。
- 缺口：没有规定哈希算法、哈希覆盖范围、加载时复核、写入原子性；现有 `MediaImporter` 先哈希后复制，存在 TOCTOU。
- 缓解：
  1. 统一 SHA-256，哈希覆盖**最终写入的完整字节**（HTML 资产是 JSON 文件字节，不是 `html` 字段）；文件名必须等于小写 hex 哈希。
  2. 写入：临时文件边写边哈希 → `fsync` → `rename` 为 `<hash>.json`；目标存在时读回校验，不覆盖。
  3. 读取：一次 `open` + `fstat` + 读全部字节 + 复核哈希；不匹配则拒绝并隔离（不自动删除）。
  4. 哈希碰撞：SHA-256 理论上可忽略；真正的风险是未校验/TOCTOU，按上述处理。不要用 MD5/SHA-1。
- 验收：`--asset-hash-test` 改名文件、内容替换、大小写哈希、哈希后替换源文件，全部被拒；写入中断不留下半文件。

#### A6 manifest/schema 滥用

- 触发：100 个组件；重复变量名；`min > max`；`default` 越界；`NaN`/`Infinity`；`size` 为负/1e9；`timeDependent` 类型错误。
- 影响：属性面板崩溃/卡死；变量求值异常；导出 NaN 帧；UI 列表爆炸。
- 计划覆盖：部分。§1.4 有变量 schema，§1.7 有 16 组件。
- 缺口：没有严格校验/拒绝策略；没有变量总数、名称正则、有限性。
- 缓解：A1/M6 的 schema；逐项拒绝并返回具体字段；未知字段忽略但记录；manifest 校验失败则资产不可用，不部分加载。
- 验收：`--html-manifest-test` 覆盖上述畸形，全部拒绝且不影响其他资产。

#### A7 未来归档/zip 导入（非 v1，但需预留）

- 触发：后续支持 `.zip` 工程包：`../` 条目、绝对路径、符号链接条目、zip bomb、重复条目。
- 影响：解压覆盖任意文件；磁盘耗尽；符号链接逃逸。
- 计划覆盖：无（当前单 JSON）。
- 缺口：v1 不实现归档，但未来一旦加入就是高危入口；现在应把限制写进格式规范，避免实现时临时补。
- 缓解：若实现，限制条目 ≤ 10,000、解压总量 ≤ 2GB、压缩比 ≤ 100:1、拒绝 `..`/绝对路径/符号链接/设备条目、逐条目 canonical 检查、先解压到临时目录再原子移动。
- 验收：未来 `--archive-test` 覆盖 zip-slip/zip-bomb/symlink。

### 2.6 数据完整性与回滚（I）

#### I1 checkpoint 缺失/损坏/被篡改

- 触发：`~/.cache` 被清理；磁盘错误；攻击者删对象/改 JSON；git 仓库损坏。
- 影响：无法回滚；恢复到错误状态；用户以为有备份实际没有。
- 计划覆盖：部分。§2.7 有列表/保留，没有完整性校验。
- 缺口：没有 hash/verify；“恢复前自动再存一个”也可能失败；没有“损坏”UI。
- 缓解：每个 checkpoint 存 sidecar SHA-256 + 大小 + 时间 + 项目 UUID；恢复前校验；失败则标记“损坏”并禁止恢复；保留最近 3 个已知良好副本（或至少不在同一目录）；索引原子写。
- 验收：`--checkpoint-corrupt-test` 改一个字节/删对象，恢复被拒并提示；索引损坏可扫描重建。

#### I2 git 不可用/版本差异/配置与 hook 风险

- 触发：没装 git；git 版本旧；`safe.directory` 拒绝；全局 `core.fsmonitor`/hook；`GIT_DIR` 环境变量。
- 影响：checkpoint 创建失败但 AI 继续写；或执行攻击者 hook。
- 计划覆盖：部分（回退快照文件）。
- 缺口：没有“checkpoint 失败是否阻止写”的规则；没有 git 加固。
- 缓解：启动时在临时目录跑一次 plumbing 自检；失败则切快照后端并显示；**写批次开始前 checkpoint 必须成功落盘，否则拒绝写**（或至少默认拒绝，人类可显式“无检查点继续”）；git 加固见 L2。
- 验收：`--checkpoint-backend-test` 隐藏 git 后仍能快照；checkpoint 写失败时 `batch.run` 返回 `checkpoint_failed` 且工程不变。

#### I3 checkpoint 与素材不一致/GC

- 触发：checkpoint 只存 `.pvproj`，素材后来被删/被 GC；恢复后 HTML 层引用的资产不存在。
- 影响：恢复成功但画面空白/导出失败；用户以为完整回滚。
- 计划覆盖：部分。§2.7 明确“只替换工程状态，不动素材”。
- 缺口：没有“素材缺失”预检；没有 GC 与 checkpoint 的引用关系；AI 禁止删除资产但人类可以。
- 缓解：checkpoint 记录引用的 asset hash 集合；恢复前列出缺失项；资产 GC（若实现）必须排除被任何保留 checkpoint 引用的资产；人类删除被 checkpoint 引用的资产时警告。
- 验收：`--checkpoint-asset-test` 删除一个被引用的 HTML 资产，恢复提示缺失；GC 后仍能恢复最近 100 个 checkpoint。

#### I4 急停竞态（apply/commit/save 各阶段）

- 触发：人类按急停时，AI 批次正在应用第 100/200 个操作、正在写 checkpoint、正在保存工程、正在等 Chrome 截图；停止事件被 UI 线程队列阻塞。
- 影响：部分应用/部分回滚；工程与 checkpoint 不一致；AI 收到 `stopped_by_human` 但实际已提交；排队请求继续执行。
- 计划覆盖：部分。§2.4 有行为列表，没有提交点定义。
- 缺口：没有 epoch/代际、没有操作边界检查、没有提交后语义、没有嵌套事件循环禁令。
- 缓解：
  1. `epoch` 单调递增；每个写请求捕获 epoch；每个 op 前和 commit 前检查；epoch 变化立即回滚并返回 `stopped_by_human`。
  2. 应用循环中禁止 `QCoreApplication::processEvents()`/模态对话框；错误收集后统一提示，避免急停事件在事务中途被处理。
  3. 定义提交点：checkpoint 落盘 + 工程 JSON 写入内存 + 响应入队。提交前停止 → 回滚；提交后停止 → 返回 `stopped_after_commit`，不自动回滚（或提供“撤销本批”按钮），避免覆盖人类刚做的编辑。
  4. 停止时：置 atomic 标志、清空队列、`Runtime.terminateExecution`/关闭 target、取消导出（杀 ffmpeg、删 `.part`）；不等待 Chrome 响应。
  5. 停止状态跨重连保持；重新武装只能人类点击。
- 验收：`--mcp-stop-race-test` 在 10 个注入点（每 op、checkpoint 前/后、commit 前/后、保存中）触发停止；提交前停止的工程哈希 == 批前；提交后返回明确状态；队列为空；无 `.part.mp4`。

#### I5 AI 与人类并发编辑/撤销栈不一致

- 触发：AI 批次运行时人类拖关键帧/改属性/按 Ctrl+Z；AI 的 undo 命令与人类命令交错。
- 影响：AI 回滚覆盖人类编辑；History 指针错乱；撤销后工程处于混合状态。
- 计划覆盖：部分。§2.4 写“AI 操作期间不能编辑”。
- 缺口：没有事务锁实现；没有定义人类编辑“漏网”时怎么办；AI 写入是否进入 History 未定义。
- 缓解：
  1. 批次期间禁用所有变更入口：属性面板、拖拽、关键帧、删除、撤销/重做、自动保存、导入；用 `QAction::setEnabled(false)` + 事件过滤器双重保险。
  2. AI 批次作为一个复合 `UndoCommand` 进入 History（保存 before/after 差异），人类一次 Ctrl+Z 撤销整批；不要每个 op 一条。
  3. 若检测到批次期间有变更（epoch/修改计数变化），立即中止并提示“检测到人工编辑，AI 批次已取消”，恢复 checkpoint 前先让人类确认。
  4. checkpoint 恢复后清空 History 或压入 `RestoreCommand`；禁止恢复后 Ctrl+Z 复活旧实体。
- 验收：`--mcp-concurrency-test` 批次中模拟人类改关键帧，AI 中止且人类编辑保留；Ctrl+Z 一次撤销整批；恢复后 undo 不会复活旧状态。

#### I6 项目身份/路径变化导致恢复错工程

- 触发：工程被重命名/复制/移动；checkpoint 目录按路径哈希；两个工程同名；Save As。
- 影响：恢复列表混入另一个工程的历史；恢复到错误工程；覆盖。
- 计划覆盖：部分。§2.7 写“工程路径哈希”。
- 缺口：路径变化即失联；复制后共享历史；没有 projectId。
- 缓解：v4 增加 `projectId`（UUID），checkpoint 目录用 projectId；恢复时校验 projectId 一致；Save As 生成新 projectId（或明确“继承历史”选项）；v3 迁移时用路径哈希并标记“旧格式”。
- 验收：`--checkpoint-identity-test` 重命名工程后历史仍可见；复制工程后不共享历史；恢复不匹配 projectId 被拒。

#### I7 崩溃/断电/磁盘满时原子性

- 触发：checkpoint 写一半断电；工程保存一半；磁盘满；进程被杀。
- 影响：JSON 截断/损坏；checkpoint 索引指向不存在对象；工程无法打开。
- 计划覆盖：部分（QSaveFile）。
- 缺口：checkpoint 没有事务顺序/fsync；索引可能先于对象；没有启动修复。
- 缓解：写入顺序：对象 → ref/索引 → `fsync` 文件 → `fsync` 目录；临时文件 `O_EXCL`；启动扫描忽略不完整条目；工程 `QSaveFile` 保持；磁盘满时 checkpoint 失败 → 拒绝写。
- 验收：`--checkpoint-crash-test` 在写对象/写索引后 `kill -9`，重启后列表一致且不崩溃；模拟 ENOSPC，写批次被拒。

#### I8 保留策略与磁盘预算

- 触发：AI 快速批次；每个 checkpoint 10–50MB；100 个 = 数 GB；磁盘剩余不足。
- 影响：磁盘满；系统其他功能失败；checkpoint 写失败导致 AI 停摆。
- 计划覆盖：部分（保留 100 个）。
- 缺口：只按数量，没有按字节/磁盘余量/最小间隔。
- 缓解：100 个且总 ≤ 2GiB；磁盘剩余 < 5GiB 停止新 checkpoint；同一批 10 秒内合并；按时间+大小双阈值裁剪；启动清理孤儿对象。
- 验收：`--checkpoint-budget-test` 写 200 个，数量/大小在预算内；磁盘配额模拟触发拒绝。

### 2.7 可用性与资源（R）

#### R1 现有限额是否足够

- 触发：4 层 × 4096² RGBA 截图 = 4×64MB = 256MB，加 Chrome 表面/PNG 编码/宿主 QImage；2MB HTML ×4；5MB 资产 ×4；16 组件/层。
- 影响：4K 多 HTML 层导出内存峰值可能 > 1GB；低内存机器 OOM。
- 计划覆盖：部分（有数字，但没有总预算/并发）。
- 缺口：没有全局像素/内存预算；没有并发截图上限；没有“限额组合”测试。
- 缓解：预览总像素 ≤ 4MP；导出总像素 ≤ 33MP（示例：4K 输出 8.3MP × 4 层；单层边长 ≤ 4096，但 4 层都按 4096² 抓会超预算，应拒绝或降层）；同时截图 ≤ 2；Chrome 总 RSS ≤ 1.5GB；宿主截图缓存 ≤ 512MB；超过拒绝并提示。
- 验收：`--html-budget-test` 4 层 4K 导出峰值 RSS 在预算内；4×4096² 请求被拒或自动降级；超预算导出被拒。

#### R2 CDP 卡死/UI 阻塞

- 触发：`Runtime.evaluate` 死循环；`Page.captureScreenshot` 卡住；Chrome 无响应；代码里 `QProcess::waitForFinished`。
- 影响：整个应用无响应；急停按钮也点不了。
- 计划覆盖：部分（超时）。
- 缺口：没有命令级超时/看门狗/隔离线程；没有禁止 UI 线程阻塞调用。
- 缓解：所有 CDP 调用带 deadline；UI 线程不 `waitFor*`；独立 watchdog 定时器；超时 `terminateExecution` → `closeTarget` → 重启；急停不依赖 CDP 响应。
- 验收：`--html-hang-test` 期间 UI 心跳 < 200ms，急停 1 秒内生效。

#### R3 Chrome 崩溃/僵尸/孤儿进程

- 触发：renderer OOM/崩溃；浏览器主进程崩溃；应用退出；`kill -9` 应用。
- 影响：孤儿 Chrome 占内存/端口；僵尸进程；下次启动端口/目录冲突。
- 计划覆盖：部分（自动重启一次）。
- 缺口：没有进程组/子进程清理；没有启动时清理残留。
- 缓解：`setsid()` + 退出 `killpg`；`QProcess` 直系子进程回收；启动时扫描带 PVForge 标记的 user-data-dir 的进程（先验证 PID/标记，避免误杀用户 Chrome）；最多重启 1 次/5 分钟，之后禁用。
- 验收：`--chrome-crash-test` kill renderer 后自动重启一次，第二次失败层禁用；kill -9 应用后，下次启动 5 秒内清理残留 Chrome（不做“被 kill -9 后立即清理”的不可能承诺）。

#### R4 磁盘满/日志无限增长/缩略图

- 触发：日志每条含 base64/HTML；缩略图不释放；导出写满磁盘。
- 影响：内存/磁盘耗尽；应用崩溃；用户其他程序受影响。
- 计划覆盖：部分（日志默认内存、可导出）。
- 缺口：没有明确上限/轮转/缩略图预算。
- 缓解：M10 的上限；导出前 `QStorageInfo::bytesAvailable` 预检（预计 ≥ 2× 输出大小 + 5GiB）；`.part` 清理；缩略图总 32MiB。
- 验收：`--log-budget-test` 100k 条后内存稳定；ENOSPC 时导出失败且清理 `.part`。

#### R5 端口冲突/绑定失败/扫描回退

- 触发：MCP 端口被占；实现模仿 CameraMonitor 扫 8080–8089 并 `Any`。
- 影响：服务起不来；或意外暴露到 LAN。
- 计划覆盖：部分（端口自动/手动）。
- 缺口：没有失败策略/loopback 强制。
- 缓解：默认随机端口（bind 0），失败即报错；用户指定端口冲突时报错，不换接口；只绑 127.0.0.1；endpoint 文件写实际端口。
- 验收：占用端口后启动失败并提示；`ss` 无 0.0.0.0 监听。

#### R6 导出停顿/取消/`.part.mp4` 清理

- 触发：HTML 层超时；Chrome 崩溃；用户取消；ffmpeg 卡住；输出路径被占用。
- 影响：导出挂起；留下半成品；取消按钮无效。
- 计划覆盖：部分（超时停止、清理 `.part`）。
- 缺口：没有逐帧/全局无进展超时；没有 symlink/覆盖保护；取消等待无上限。
- 缓解：逐帧 10s、无进展 60s；取消杀 ffmpeg + `waitForFinished(2s)` + `kill`；输出用同目录随机临时名，成功后 `rename`；拒绝 symlink/已存在文件（或明确覆盖确认）。
- 验收：`--html-export-timeout-test` 超时后无 `.part` 残留；取消 2 秒内返回；symlink 输出被拒。

#### R7 浏览器 profile 目录增长/清理

- 触发：每次崩溃留下 user-data-dir；缓存/日志增长。
- 影响：磁盘占用；隐私残留。
- 计划覆盖：无。
- 缺口：没有目录生命周期/大小上限/残留清理。
- 缓解：每次会话新目录，退出删除；启动清理 > 7 天且带标记的目录；单目录 ≤ 2GB 警告；不把工程数据写入 profile。
- 验收：`--chrome-profile-cleanup-test` 创建 10 个假残留，启动后只清理带标记且过期的。

#### R8 队列/连接/预览请求积压

- 触发：AI 高速调用预览/写工具；SSE 连接不读；多个客户端。
- 影响：内存增长；旧帧延迟；UI 卡。
- 计划覆盖：部分（§2.3 有预览 2 次/秒、AI 失败阈值）。
- 缺口：没有连接/会话/队列上限、没有 SSE 背压、没有 latest-wins 策略。
- 缓解：连接 16、会话 4、写队列 32、预览 latest-wins、SSE 每会话 1、空闲 10 分钟；队列满 429；日志队列丢弃最旧并计数。
- 验收：`--mcp-backpressure-test` 1000 次预览只保留最新；写队列满返回 429；内存稳定。

#### R9 内存中的 undo/checkpoint 快照含 QImage

- 触发：AI 批次创建 50 个实体，每个 Entity 含 4K `image`/`thumbnail`/`pixmap`；`AddEntityCommand`/`RemoveEntityCommand` 复制整个 Entity。
- 影响：一次批次可能占用数 GB；checkpoint 序列化时若带上运行时缓存也会膨胀。
- 计划覆盖：无。
- 缺口：现有命令按值复制 `Entity`（含 `QImage`、`QPixmap`、`shared_ptr<AudioClip>`）；`Project::toJson` 已排除运行时缓存，但 undo 没有。
- 缓解：快照/命令中剥离运行时字段（`image`、`thumbnail`、`pixmap`、`halftonePixmap`、`audio`、`wallLogos`），只保留 `sourcePath`/`data`/`tracks`/`values`；需要时懒加载；History 总量上限（例如 500 条/512MB）。
- 验收：`--m6-undo-memory-test` 50 个 4K 实体批次后 RSS 增长 < 100MB；撤销后图像懒加载正确。

## 3. 对计划安全承诺的逐条评估

| 承诺 | 计划原文/位置 | 红队判断 | 成立条件 | 必须补的验证 |
|---|---|---|---|---|
| 断网 | §1.7“Chrome 启动参数 + CDP 拦截所有非 data:/内联请求” | **不成立**。Chrome 会忽略未知参数；IP 字面量绕过 DNS；Fetch 不覆盖 WebSocket/WebRTC；代理环境变量/后台联网未封 | CSP `connect-src 'none'` + sandbox iframe；黑洞代理 + host-resolver；Linux netns；注入数据最小化 | 启动 canary：fetch/img/WS/WebRTC 全部 0 连接；失败则禁用 HTML |
| 禁 file | §1.7“禁止 file://、本地路径” | **不成立**。没有可靠 Chrome 参数；组件自导航/`<iframe>` 可能尝试 | sandbox opaque origin + 无 top-navigation + `pvf.asset` 只回 data:；`Page.frameNavigated` 监控 | `fetch('file:///etc/hostname')` 拒绝；导航后 URL 不变；下载 0 |
| 禁 WebGL | §1.4“禁用 WebGL、视频、音频” | **有条件成立**。`--disable-gpu` 不等于禁 WebGL（可能走 SwiftShader）；需 `--disable-3d-apis` | 参数 + 行为探针；失败关闭 | `canvas.getContext('webgl'/'webgl2')` 必须为 null；否则禁用 HTML |
| 确定性 clock | §1.5 固定 Date/performance、seed Math.random、禁 CSS 动画、fonts.ready、两次 rAF | **部分成立**。Worker/iframe 新 realm、`crypto.getRandomValues`、rAF 时间戳、setTimeout、Intl/TZ、字体回退、`pvf.rendered` 伪造都可能破坏 | document-start 注入所有 frame；禁 Worker/iframe；覆盖 crypto/timers/rAF；CDP `Animation.setPlaybackRate(0)`；隔离世界等待；bundle 字体 | 同帧两次解码像素哈希一致；换 TZ 一致；延迟图片/字体测试；页面尝试真实时间 API 仍一致 |
| 场景边界 | §2.3“当前播放头所在场景的根文件夹” | **方向对，执行层不完整**。播放头被人类移动、场景空隙、目标实体祖先、scene.create 扩权、全局对象都未定义 | scope 在解锁时快照；按目标实体根祖先校验；空隙拒绝；全局单独解锁；切场景自动解除 | 跨场景改/移/建全部拒绝；空隙拒绝；重连/重开不保留旧 scope |
| 只读默认 | §2.1/§2.3“默认只读；显式允许修改” | **部分成立**。`tools/list` 隐藏不等于执行拒绝；直接 `tools/call` 可绕过；重连/initialize 可能重置 | 服务端状态机 + 执行时检查 + 状态跨重连；权限绑定 project/scene/session；过期 | 只读下直接调用每个写工具均 `permission_denied`；重连后仍只读 |
| 急停 | §2.4“中止当前调用、清空队列、恢复批次快照” | **部分成立**。UI 线程事务中无法立即中断；提交后回滚语义未定义；嵌套事件循环/模态框会延迟；Chrome 调用可能卡住 | epoch + 每 op/commit 检查；禁 processEvents；定义提交点；停止不依赖 CDP；跨重连保持 | 10 个注入点停止；提交前哈希一致；提交后返回明确状态；队列空；无 `.part` |
| checkpoint | §2.7“每次批次隐藏 git checkpoint + 恢复列表” | **部分成立**。只存工程 JSON（素材不动是设计）；没有“写前必须成功”的规则；git hook/config、项目身份、磁盘预算/完整性未覆盖 | 写前 checkpoint 成功才允许写；项目 UUID；plumbing-only git + 禁 hook；sidecar hash；字节预算 | checkpoint 失败时写被拒；hook 不执行；损坏检测；重命名/复制工程历史正确 |

补充：计划 §1.7 的“同一帧两次逐像素一致”是**同机同浏览器版本**的验收，不应承诺跨机器/跨版本一致（字体、GPU、Chrome 版本都会变）。文档应写明边界。

## 4. 优先级

### P0：上线前必须完成（缺一不可）

| ID | 项 | 原因 |
|---|---|---|
| B4/B5 | CDP 走 pipe，禁止 remote-debugging 端口 | 否则本机任何进程（含其他用户）可完全控制浏览器，绕过所有 MCP/HTML 防护 |
| H1/H2/H3 | sandbox iframe + CSP + 断网/file/WebGL canary，失败关闭 | 计划的核心“安全渲染”依赖这三条；参数不可信，必须行为验证 |
| H3 | 禁止 `--no-sandbox`，沙箱不可用则禁用 HTML | 0-day 逃逸是最高影响；不能为了可用性静默降级 |
| M2/M3/M4 | loopback-only + Host/Origin + Bearer + 严格 HTTP 限额 | 防 LAN/本机其他用户/DNS rebinding/走私；MCP 是写入口 |
| M8/M9 | 执行层场景边界 + 权限状态机 + 跨重连保持 | 否则“只读/当前场景”只是 UI 文案 |
| I4/I5 | 急停 epoch/提交点 + 人类编辑锁 + 复合 undo | 否则急停会半回滚/覆盖人类工作 |
| I2/I3/I6 | 写前 checkpoint 必须成功 + 项目 UUID + 素材引用预检 | 没有可靠回滚就不应允许 AI 写 |
| L5/A5/A2 | 路径 canonical/符号链接拒绝 + 哈希复核 + 导入复制原子化 | 防任意文件读写和资产替换 |
| A1/A2/A3 | 项目/素材加载限额与特殊文件拒绝 | 恶意工程是现实入口，现有 `readAll()` 无上限 |
| H4/H5/R2/R3 | CPU/内存看门狗 + CDP 超时 + 进程组清理 | 否则一个 HTML 就能拖死应用/机器 |
| L6 | 不可信工程默认不自动渲染 HTML | 打开即执行是最容易被忽视的入口 |

### P1：首个迭代

- M5/M7 速率/配额/批次数；M10 日志转义/上限；M11 预览范围/同意/分辨率；M12 桥接校验。
- L3 外部修改检测/自动保存暂停；L4 注释注入标注；I1/I7/I8 checkpoint 完整性/预算/崩溃修复。
- R1 像素/内存总预算；R4 日志/缩略图/磁盘预检；R6 导出临时文件/取消；R7 profile 清理；R8 背压。
- R9 快照剥离 QImage；History 总量上限。
- A4/A6 资产解码/ manifest 严格校验；A7 归档预留（如实现）。
- Windows/macOS 的签名/ACL/quarantine 具体实现；Linux netns/ bwrap 严格模式。
- 现有 `CameraMonitor` 的 `QHostAddress::Any` 无认证串流（见附录）至少加警告/改为默认 loopback，避免被当作 M6 的模板。

### P2：后续

- checkpoint 加密/远程备份；每工程信任库；VM/独立用户隔离；代码签名全链路；模糊测试（HTTP/JSON/图片/HTML）；SBOM/依赖更新自动化；Canvas/字体更深层沙箱。
- 若未来支持 zip/归档、局域网 MCP、远程 AI，需要单独威胁评审（TLS、认证、配额、审计）。

## 5. 最小安全验收清单（可转成 `--m6-security-test`）

建议一个自检入口按组输出 PASS/FAIL；任一项 FAIL 时 CI 失败。每组可在无 Chrome 环境标记 SKIP，但不能把 SKIP 当 PASS。

### 5.1 浏览器启动与沙箱

- [ ] 命令行含 `--remote-debugging-pipe`，不含 `--remote-debugging-port`、`--remote-allow-origins`、`--no-sandbox`、`--single-process`、`--allow-file-access-from-files`、`--disable-web-security`。
- [ ] `--user-data-dir` 在 `~/.cache/PVForge/chrome/` 下，目录 0700，不等于任何默认 Chrome profile。
- [ ] 子进程环境不含 `LD_PRELOAD`/`DYLD_*`/`http_proxy`/`https_proxy`/`all_proxy`；`PATH` 已最小化。
- [ ] 启动后本进程树没有监听 TCP 端口（pipe 模式）；9222/9229 连接被拒。
- [ ] Linux：沙箱可用（setuid chrome-sandbox 或 userns）；不可用时 HTML 禁用而非 `--no-sandbox`。
- [ ] `geteuid()==0` 时 HTML 禁用。
- [ ] Chrome/Edge `--version` 输出通过允许列表；非标准路径有警告/确认。
- [ ] 正常退出后 5 秒内无 PVForge 标记的 Chrome 残留进程；崩溃后下次启动 5 秒内清理。

### 5.2 HTML 隔离与行为

- [ ] 组件在 `sandbox="allow-scripts"` iframe 中；`window.parent.document`、`window.top.pvf` 抛 `SecurityError`。
- [ ] `fetch('file:///etc/hostname')`、`fetch('http://127.0.0.1:<canary>')`、`new WebSocket('ws://127.0.0.1:<canary>')`、`<img src="http://...">`、WebRTC 探测全部失败；canary 连接计数 0。
- [ ] `canvas.getContext('webgl')`、`'webgl2'`、`'experimental-webgl'` 均为 null。
- [ ] `new Worker()`/`SharedWorker`/`ServiceWorker` 被 CSP 拒绝。
- [ ] `window.open`、`<a download>`、`navigator.clipboard.writeText`、`Notification.requestPermission` 全部无效；下载计数 0。
- [ ] `location='file:///etc/hostname'` 后 frame URL 不变；`location.reload()` 超过 3 次 target 被关闭。
- [ ] `document.cookie` 为空/不可用；`localStorage` 抛异常；关闭后不残留。
- [ ] `pvf.vars` 只含当前层 manifest 声明的变量；`pvf.asset('other')` 返回 undefined。
- [ ] `pvf.rendered()` 提前调用时，仍等待图片 decode + 两次隔离世界 rAF 后截图；延迟图片测试得到最终图。
- [ ] 同帧两次渲染，解码后 RGBA 像素哈希一致；改 `TZ` 后仍一致；死循环 2 秒内占位且 UI 心跳 < 200ms。

### 5.3 MCP 认证与传输

- [ ] 无 token/错 token/query token → 401；旧 token（轮换后）→ 401。
- [ ] 监听地址为 `127.0.0.1`（或 `::1`），非 loopback 连接被拒绝；`ss` 无 `0.0.0.0`。
- [ ] `Host: evil.com` → 403；`Origin: https://evil.com` → 403；跨站 `Sec-Fetch-Site` → 403；无 CORS 响应头。
- [ ] `Content-Length`+`Transfer-Encoding`、双 CL、负 CL、CL > 8MiB → 400/413 并关闭连接。
- [ ] header > 16KiB、body > 8MiB、JSON 深度 > 64、字符串 > 1MiB → 拒绝。
- [ ] 100 个并发连接被限制在 16；慢 header 5 秒关闭；慢 body 15 秒关闭；UI 心跳 < 200ms。
- [ ] `POST /other` → 404；`GET /mcp`（非 SSE）→ 405；JSON-RPC batch 数组 → 拒绝或 ≤ 20。
- [ ] endpoint 文件 0600、目录 0700；`grep` 日志/argv/配置无 token；桥接拒绝 0644/错 PID/非 loopback endpoint。

### 5.4 权限、场景与急停

- [ ] 只读模式下直接 `tools/call` 每个写工具 → `permission_denied`；`tools/list` 不含写工具。
- [ ] 场景 A 激活时：改/移/建场景 B 实体 → 拒绝；播放头在空隙 → `no_active_scene`；`entity.move` 移出场景根 → 拒绝；`activeCamera` 未解锁 → 拒绝。
- [ ] 人类把播放头移到场景 B 后，AI 的 scope 不变或自动解除武装；不会写 B。
- [ ] `initialize`/重连不改变 Stopped/ReadOnly；急停后重连写仍拒绝；跨 session cancel 被拒。
- [ ] 批次 201 ops → 拒绝；第 200 个 op 失败时工程 JSON 哈希 == 批前；checkpoint 存在且可恢复。
- [ ] 急停在 10 个注入点：提交前工程哈希 == 批前；提交后返回 `stopped_after_commit`；队列空；无 `.part.mp4`。
- [ ] 批次期间人类编辑被禁用；Ctrl+Z 一次撤销整批；checkpoint 恢复后 undo 不复活旧状态。

### 5.5 工程/资产加载

- [ ] 65MiB 工程、5001 实体、10,001 键/轨、1MiB 字符串、version 5 → 拒绝且应用不崩。
- [ ] `sourcePath: "../../x"`、`/etc/shadow`、`/dev/zero`、FIFO、网络路径 → 拒绝或需明确确认；HTML 资产绝对路径/`..` 拒绝。
- [ ] `assets` 为符号链接 → 拒绝；资产文件为符号链接 → 拒绝；哈希不匹配 → 拒绝。
- [ ] 20000×20000 PNG、1000 帧 WebP、>4h 视频 → 拒绝；内存不暴涨。
- [ ] HTML manifest：17 组件、257 变量、重复变量、min>max、NaN、size 1e9 → 拒绝。
- [ ] 不可信工程（/tmp、Downloads、quarantine/MOTW）打开不自动启动 Chrome；确认后才渲染。

### 5.6 完整性与资源

- [ ] checkpoint 损坏/对象删除/哈希不符 → 恢复被拒并提示；git hook 放置后不执行；隐藏 git 后走快照后端。
- [ ] 100 个/2GiB checkpoint 预算生效；磁盘剩余 <5GiB 时写被拒；checkpoint 失败时批次不提交。
- [ ] 重命名/复制工程后 checkpoint 身份正确；恢复不匹配 projectId 被拒。
- [ ] 4 层 4K 导出峰值内存/像素在预算内；4×4096² 被拒或降级；同时截图 ≤ 2；预览 2/s 限流；响应 ≤ 8MiB。
- [ ] kill renderer → 自动重启一次；第二次失败层禁用；kill -9 应用后下次启动清理残留。
- [ ] 日志 100k 条后内存稳定；换行/ANSI/RTL/1MB 名称不破坏日志 UI；JSONL 可解析。
- [ ] 自动保存前外部修改不覆盖；导出取消 2 秒内返回；ENOSPC 清理 `.part`。

## 6. 对 M6 计划的修改建议

1. **§1.1 浏览器生命周期**：明确 `--remote-debugging-pipe`（P0）、禁止 remote-debugging 端口、禁止 `--no-sandbox`、沙箱不可用则禁用 HTML；列出固定探测路径与路径校验（owner/权限/签名）；子进程环境清理与 `setsid`/rlimit；启动 canary 失败关闭。
2. **§1.4 组件契约**：把“组件运行在 sandbox iframe + CSP”写成硬约束；`pvf.asset()` 只返回 data: 且按层白名单；禁止 Worker/iframe/下载/弹窗/剪贴板；宿主与组件只用 `postMessage`，校验 source/origin/schema。
3. **§1.5 确定性**：补 document-start 注入所有 frame、禁 Worker/iframe、覆盖 `crypto`/timers/rAF 时间戳、`Animation.setPlaybackRate(0)`、隔离世界等待、`document.images.decode()`、bundle 字体/TZ=UTC、PNG 解码后比较；明确“同机同版本”边界。
4. **§1.7 安全与限制**：把“断网”改为“多层防护 + canary 验证”；补 IP 字面量/WebSocket/WebRTC/代理环境变量；补 CPU/RSS/像素总预算、DOM/Canvas 上限、连续失败禁用；补不可信工程默认不渲染。
5. **§2.1 MCP 服务器**：endpoint 改到 runtime dir（0700/0600）；Host/Origin/CORS/Content-Type 校验；HTTP 限额表（header 16KiB、body 8MiB、连接 16、超时 5/15/30s）；token 轮换/日志脱敏/常量时间比较；优先 Unix socket + stdio 桥。
6. **§2.2 工具边界**：每个工具列严格 schema/范围/上限；`assets.import_blob` 只收 base64/MIME/显示名；禁止 data 注入 `sourcePath`；HTML manifest 限额；响应分页/大小上限。
7. **§2.3 权限与边界**：写 scope 在解锁时快照 `{projectUuid, sceneId, sceneRootId, frameRange}`；按目标实体根祖先校验；空隙拒绝；全局对象单独解锁或 v1 不提供；写速率/每批新建/关键帧/每会话总量；权限过期与项目切换降级。
8. **§2.4 急停**：定义 epoch、提交点、提交前/后语义；禁止事务中 `processEvents`/模态框；停止不依赖 CDP；跨重连保持；10 个注入点测试。
9. **§2.7 checkpoint**：写前 checkpoint 必须成功才允许提交；项目 UUID（v4）替代路径哈希；git plumbing-only + 禁 hook/系统配置；sidecar SHA-256；100 个/2GiB/磁盘 5GiB 预算；素材引用集合与恢复预检；崩溃顺序/fsync。
10. **§4 工程格式 v4**：增加 `projectId`；加载限额（64MiB/5000 实体/200k 关键帧/字符串上限）；版本 1–4 且 >4 拒绝；未知实体类型拒绝而非当 Image；路径 canonical/特殊文件/符号链接规则；HTML 资产哈希复核。
11. **§5 实施阶段**：Stage 0 spike 增加 canary（断网/file/WebGL/沙箱/pipe）与内存/CPU 看门狗原型；Stage 3 增加 HTTP 畸形/慢连接测试；Stage 4 增加 stop race/人类并发/checkpoint 失败测试；Stage 5 增加 `--m6-security-test`。
12. **§6 风险表**：把“断网”“只读默认”“急停”“checkpoint”从“对策”改成“需运行时验证的不变量”，并列出验证入口；增加“Chrome 参数漂移导致 fail-open”“本地端口 CDP 暴露”“不可信工程打开即渲染”三行。
13. **§7 测试清单**：新增 `--m6-security-test`（第 5 节）、`--html-canary-test`、`--html-hang-test`、`--html-memory-test`、`--mcp-http-test`、`--mcp-scope-test`、`--mcp-stop-race-test`、`--checkpoint-corrupt-test`、`--asset-path-test`、`--project-limit-test`、`--untrusted-project-test`、`--m6-undo-memory-test`。
14. **§8 默认值**：补“CDP pipe、无监听端口、不可信工程默认不渲染、预览仅当前场景、写权限 30 分钟过期、checkpoint 失败拒绝写、日志/缩略图/checkpoint 字节预算”。

## 7. 附录：现有代码中与 M6 直接相关的发现

这些不是 M6 新增代码，但会被 M6 继承或作为实现模板，建议在 M6 前/同期修：

| 位置 | 问题 | 影响 | 建议 |
|---|---|---|---|
| `src/ui/CameraMonitor.cpp:987` | `m_httpServer->listen(QHostAddress::Any, port)` 扫 8080–8089 | 局域网任何人可无认证拉流；默认暴露面 | 改为默认 `LocalHost` + 明确“允许局域网”开关；至少 UI 警告 |
| `src/ui/CameraMonitor.cpp:1113` | 任意 HTTP 请求（任意路径/方法）都返回 200 流 | 端口扫描/滥用；无来源校验 | 校验 `GET /stream.ts`、Host、可选一次性 token；限制连接数 |
| `src/core/Project.cpp:1525` | `file.readAll()` 无大小上限 | 恶意/损坏工程 OOM | 先 `size()` 检查 + 流式/限额解析 |
| `src/core/Project.cpp:1571` | 实体/轨道/关键帧数量无上限 | 加载卡死/内存爆炸 | A1 的限额 |
| `src/core/Project.cpp:1547` | 完全不读 `version` | 新版/恶意版本静默加载 | 接受 1–4，>4 拒绝 |
| `src/core/Project.cpp:1584` | `sourcePath` 绝对路径/`..` 原样接受 | 任意文件读取 | canonical + 根内限制 + 特殊文件拒绝 |
| `src/core/Project.cpp:1652` | `QImage(e.sourcePath)` 无尺寸预检 | 图片炸弹/内存 | `QImageReader` 先读尺寸 + 分配上限 |
| `src/core/Project.cpp:127` | `resolveAssetPath` 不解析符号链接/不限制根 | 路径穿越 | canonical + `lstat` |
| `src/media/MediaImporter.cpp:73` | 先 `sha256File()` 再 `QFile::copy()` | TOCTOU：文件名哈希可能与内容不符；符号链接未解析 | 边复制边哈希到临时文件，最后 rename |
| `src/app/MainWindow.cpp:686` | 自动保存无条件覆盖 | 覆盖外部修改/同步冲突 | 保存前 stat+hash 检查，变化则暂停 |
| `src/core/History.cpp:26` | `redo()` 后直接入栈，无失败回滚；History 无上限 | 批次中途失败留下半状态；长会话内存增长 | 复合命令 + 应用前校验 + 总量上限 |
| `src/core/Commands.h` 各命令按值存 `Entity` | 含 `QImage`/`QPixmap`/`AudioClip` | AI 大批量快照内存爆炸 | 快照剥离运行时缓存（R9） |
| `src/ui/CameraMonitor.cpp:1014` | `QStandardPaths::findExecutable("ffmpeg")` 走 PATH | PATH 劫持先例 | 固定路径/校验；M6 的 Chrome 不要复制此模式 |

## 8. 评审限制

- 未运行应用、未做 Chrome/CDP 实测、未做 HTTP/JSON/图片 fuzz；本文的“验收”是建议的自检项，不是已通过的结果。
- Chrome 参数在不同版本/平台行为不同；所有“参数”都必须由 canary 行为验证，不能把参数存在当作防护生效。
- 同一用户下的恶意进程可以读 0600、连 loopback、改 QSettings；本文不承诺防御这一层，只缩小影响窗口（轮换、过期、确认、审计、最小注入）。
- 跨机器/跨 Chrome 版本的像素级确定性不在本文承诺范围内；计划应明确“同机同版本”边界。
