# M6 Stage 0：Chrome/CDP spike 测量报告

记录时间：2026-11-08T09:12:16.360Z
主机：Intel(R) Core(TM) Ultra 5 125H，30.9 GiB RAM，linux 7.0.0-43-generic
系统负载（1/5/15 分钟）：开始 36.21/19.74/9.97，结束 24.75/18.36/9.77
Node：v25.9.0

## 结论

- 1080p 单帧总耗时中位数 **86.97 ms**（门槛 ≤ 25 ms）：**NO-GO**。
- 720p 单帧总耗时中位数 **61.23 ms**：**NO-GO**。
- 等效帧率：720p 16.3 fps，1080p 11.5 fps（按中位数；p95 69.86 / 102.33 ms）。
- 即使不计解码，1080p 截图环节中位 **53.51 ms**、720p **43.10 ms**，仍高于 25 ms 门槛；结论不依赖 Node 解码器的速度。
- 正确性（确定性、透明、内联图片、无空白/旧帧、断网、file://）：**全部通过**。

**建议**

- 预览按显示尺寸抓帧并限 15 fps；1080p 全画质预览不要按 30 fps 承诺。
- 导出不节流，逐帧等待抓图；以当前中位数估算，1080p 导出上限约 11.5 fps，720p 约 16.3 fps（不含编码）。
- 若导出吞吐不够，优先降低抓帧分辨率（按输出尺寸的 0.5–0.75 抓再放大），而不是减少帧数。
- 每层一个 page 时，后台标签页的截图会永久挂起；抓帧前必须 `Page.bringToFront`（详见第 4 节）。

## 1. 环境与启动参数

- Chrome：`/usr/bin/google-chrome`
- 版本：`Google Chrome 157.0.8103.38`
- 进程启动耗时：166.48 ms（到 DevTools URL 可连接）
- 启动参数：`--headless=new --remote-debugging-port=0 --remote-allow-origins=* --no-first-run --no-default-browser-check --disable-background-networking --disable-component-update --disable-sync --disable-extensions --disable-default-apps --disable-dev-shm-usage --disable-gpu --hide-scrollbars --mute-audio --force-device-scale-factor=1 --force-color-profile=srgb --host-resolver-rules=MAP * ~NOTFOUND --disable-features=Translate,MediaRouter,OptimizationHints,CalculateNativeWinOcclusion --user-data-dir=/tmp/pvforge-spike-profile-KUUlvx`
- Edge：`/usr/bin/microsoft-edge`
- 版本：`Microsoft Edge 154.0.4258.48`
- Edge 进程启动耗时：238.51 ms
- Edge 启动参数：`--headless=new --remote-debugging-port=0 --remote-allow-origins=* --no-first-run --no-default-browser-check --disable-background-networking --disable-component-update --disable-sync --disable-extensions --disable-default-apps --disable-dev-shm-usage --disable-gpu --hide-scrollbars --mute-audio --force-device-scale-factor=1 --force-color-profile=srgb --host-resolver-rules=MAP * ~NOTFOUND --disable-features=Translate,MediaRouter,OptimizationHints,CalculateNativeWinOcclusion --user-data-dir=/tmp/pvforge-spike-profile-DKHAmB`

抓帧约定：

- 页面通过 `Page.setDocumentContent` 注入；字体只用 `sans-serif` / `monospace`，不加载外部资源。
- 确定性 stub（固定 `Date.now` / `performance.now`、按帧重置 `Math.random`、关闭 CSS 动画）内联为文档的第一个 `<script>`。`Page.addScriptToEvaluateOnNewDocument` 对 `Page.setDocumentContent` 不生效，生产实现必须内联，或改用真实导航 / data: URL。
- CSS animation / transition 由注入样式强制关闭；`renderFrame` 等待 `document.fonts.ready` 后才调用 `pvf.rendered()`。
- 页面内有一张内联 `data:` SVG（48×48），用于验证“内联资源可用、外部网络不可用”；它不参与进度条像素检查。
- 每帧流程：`Runtime.evaluate(awaitPromise)` → `Page.bringToFront` → `Page.captureScreenshot`（PNG，clip）→ base64 + PNG 解码为 RGBA。
- “解码”列 = base64 解码 + zlib inflate + 反滤波，对应 Qt `QImage::loadFromData` 的主要工作。
- **传输与安全**：本 spike 用 `--remote-debugging-port=0`（loopback 随机端口）+ WebSocket，便于测量 CDP 往返；`--remote-allow-origins=*` 仅为 spike 方便。生产实现按安全评审改用 `--remote-debugging-pipe`（stdio fd 3/4），不开 TCP 端口、不需要 `--remote-allow-origins=*`；CDP 消息与截图路径不变，延迟差异应在 Stage 1 复测。

## 2. 单页连续帧（Chrome）

| 场景 | 分辨率 | 阶段 | min | median | p95 | max | mean |
|---|---|---|---|---|---|---|---|
| chrome-720p | 1280×720 | 注入+渲染 | 1.25 | 1.84 | 2.35 | 3.21 | 1.86 |
| chrome-720p | 1280×720 | 截图（CDP） | 30.55 | 43.10 | 50.89 | 56.23 | 42.01 |
| chrome-720p | 1280×720 | 解码（PNG→RGBA） | 12.78 | 15.13 | 21.85 | 27.47 | 15.95 |
| chrome-720p | 1280×720 | 总计 | 44.76 | 61.23 | 69.86 | 76.03 | 59.81 |
| chrome-720p | 1280×720 | 等效 fps（中位/P95） |  | 16.3 | 14.3 |  |  |
| chrome-1080p | 1920×1080 | 注入+渲染 | 1.34 | 2.15 | 2.83 | 4.21 | 2.21 |
| chrome-1080p | 1920×1080 | 截图（CDP） | 43.03 | 53.51 | 64.88 | 77.65 | 54.12 |
| chrome-1080p | 1920×1080 | 解码（PNG→RGBA） | 29.01 | 30.64 | 37.84 | 57.57 | 31.83 |
| chrome-1080p | 1920×1080 | 总计 | 76.39 | 86.97 | 102.33 | 109.00 | 88.17 |
| chrome-1080p | 1920×1080 | 等效 fps（中位/P95） |  | 11.5 | 9.8 |  |  |

每场景先跑 5 帧预热（不计入统计），随后连续抓取完整帧数。

| 场景 | 帧数 | 页面就绪 | 平均 PNG 大小 |
|---|---|---|---|
| chrome-720p | 100 | 82.80 ms | 10.6 KiB |
| chrome-1080p | 100 | 52.18 ms | 15.7 KiB |

## 3. 正确性

| 检查 | chrome-720p | chrome-1080p |
|---|---|---|
| 同帧两次 raw RGBA 哈希一致（ffmpeg 解码） | 通过 | 通过 |
| 内置 PNG 解码与 ffmpeg 像素一致 | 通过 | 通过 |
| 透明背景（角落 alpha=0，进度条 alpha=255） | 通过 | 通过 |
| 内联图片（data: URL SVG）渲染 | 通过 | 通过 |
| 无空白帧（不透明像素 > 1000） | 通过 | 通过 |
| 无旧帧/错帧（逐帧检查进度条像素） | 通过 | 通过 |
| 网络请求被拦截（fetch 失败） | 通过 | 通过 |
| file:// 被拦截（fetch 失败） | 通过 | 通过 |

- chrome-720p：角落 alpha=0，进度条 alpha=255，内联图标 alpha=255/g=199，不透明像素=12452，网络探测={"http":"blocked","file":"blocked"}
- chrome-720p：raw RGBA SHA-256（前 16 位）A=`f054885fe029f1d4`，B=`f054885fe029f1d4`；PNG 字节相同=是（仅参考，不作判据）
- chrome-1080p：角落 alpha=0，进度条 alpha=255，内联图标 alpha=255/g=199，不透明像素=12452，网络探测={"http":"blocked","file":"blocked"}
- chrome-1080p：raw RGBA SHA-256（前 16 位）A=`b49f7ad5b9887e5a`，B=`b49f7ad5b9887e5a`；PNG 字节相同=是（仅参考，不作判据）

说明：PNG 元数据（时间戳/压缩块）不参与比较；确定性检查统一用 `ffmpeg -pix_fmt rgba` 解码后的字节哈希。

## 4. 两个 page 同时存在

| 场景 | 分辨率 | 轮数 | 每轮两页总耗时 min | median | p95 | max | mean | 进程树 RSS |
|---|---|---|---|---|---|---|---|---|
| two-pages-720p | 1280×720 | 25 | 122.58 | 150.43 | 166.78 | 172.75 | 151.79 | 1669 MiB / 16 procs |
| two-pages-1080p | 1920×1080 | 12 | 165.25 | 188.20 | 232.89 | 232.89 | 193.02 | 1675 MiB / 16 procs |

每轮 = page A 注入+截图+解码，再 page B 注入+截图+解码。**两个 page 在同一浏览器内时，后台标签页不会产生合成帧；每次截图前必须 `Page.bringToFront`，否则 `Page.captureScreenshot` 永久挂起。** 因此双页抓帧是串行的，不能并行。

## 5. 内存

- 浏览器启动后（无页面）：865 MiB / 10 个进程
- 单页测量结束：1438 MiB / 14 个进程
- two-pages-720p（两页）：1669 MiB / 16 个进程
- two-pages-1080p（两页）：1675 MiB / 16 个进程

RSS 为进程树求和，共享内存会被重复计数，只能作为上界参考。

## 6. Edge smoke

| 场景 | 分辨率 | 帧数 | 注入+渲染 中位/P95 | 截图 中位/P95 | 解码 中位/P95 | 总计 中位/P95 | 等效 fps |
|---|---|---|---|---|---|---|---|
| edge-720p | 1280×720 | 30 | 2.09 / 2.74 ms | 46.30 / 52.21 ms | 13.29 / 19.08 ms | 62.06 / 67.97 ms | 16.1 |

| 检查 | 结果 |
|---|---|
| edge-720p 同帧像素一致 | 通过 |
| edge-720p 透明背景 | 通过 |
| edge-720p 内联图片 | 通过 |
| edge-720p 无空白/旧帧 | 通过 |
| edge-720p 网络/file 拦截 | 通过 |

## 7. 样本 PNG

- chrome-720p 首帧：`/tmp/pvforge-spike-shots/chrome-720p-first.png`
- chrome-720p 中间帧：`/tmp/pvforge-spike-shots/chrome-720p-mid.png`
- chrome-720p 末帧：`/tmp/pvforge-spike-shots/chrome-720p-last.png`
- chrome-720p 确定性 A/B：`/tmp/pvforge-spike-shots/chrome-720p-det-a.png` / `/tmp/pvforge-spike-shots/chrome-720p-det-b.png`
- chrome-1080p 首帧：`/tmp/pvforge-spike-shots/chrome-1080p-first.png`
- chrome-1080p 中间帧：`/tmp/pvforge-spike-shots/chrome-1080p-mid.png`
- chrome-1080p 末帧：`/tmp/pvforge-spike-shots/chrome-1080p-last.png`
- chrome-1080p 确定性 A/B：`/tmp/pvforge-spike-shots/chrome-1080p-det-a.png` / `/tmp/pvforge-spike-shots/chrome-1080p-det-b.png`
- edge-720p 首帧：`/tmp/pvforge-spike-shots/edge-720p-first.png`
- edge-720p 中间帧：`/tmp/pvforge-spike-shots/edge-720p-mid.png`
- edge-720p 末帧：`/tmp/pvforge-spike-shots/edge-720p-last.png`
- edge-720p 确定性 A/B：`/tmp/pvforge-spike-shots/edge-720p-det-a.png` / `/tmp/pvforge-spike-shots/edge-720p-det-b.png`

## 8. 发现与限制

1. **后台标签页截图会挂起。** headless Chrome 只为前台标签页合成帧；对后台 page 调 `Page.captureScreenshot` 会一直等到 15 s 超时。`--disable-backgrounding-occluded-windows`、`--disable-renderer-backgrounding`、`--disable-background-timer-throttling` 都不能解决，只有 `Page.bringToFront` / `Target.activateTarget` 有效。生产实现必须：抓帧前激活目标 page；多 HTML 层串行抓帧，或每层独立浏览器进程（RSS 更高）。
2. `requestAnimationFrame` 在 headless 下不可靠；本 spike 用 `setTimeout(0)` + 强制 layout 后调用 `pvf.rendered()`，并用逐帧像素检查证明不是旧帧。生产契约可保留“`rendered()` + 一次强制样式刷新”，不要依赖 rAF 次数。
3. `Page.addScriptToEvaluateOnNewDocument` 不会在 `Page.setDocumentContent` 时执行；宿主必须把确定性 stub 内联进 HTML 文档开头，或改用真实导航 / `data:` URL。
4. `--run-all-compositor-stages-before-draw` 在本机没有帮助，已从启动参数移除；保留它会增加等待合成帧的风险。
5. `Page.captureScreenshot` 的 PNG 编码是主要成本；`optimizeForSpeed`、`fromSurface:false` 在 Chrome 157 上没有改善（后者更慢）。
6. 单页连续抓帧时不需要 `Page.bringToFront`；多页时必须。
7. 同一帧两次像素一致依赖：固定 `Date.now`/`performance.now`、按帧重置 PRNG、禁用 CSS 动画/过渡、内联全部资源、等待字体。缺一不可。
8. 本报告的“解码”用 Node 内置 zlib 解码器计时，是 Qt `QImage::loadFromData` 的上界；截图环节本身的 53.51 ms（1080p）已足以判定 25 ms 门槛不成立。
9. 测量期间系统还有其它 CPU 负载（见开头负载值），绝对值会随机器忙闲波动；即便如此，1080p 截图环节的最小值仍有 43.03 ms，25 ms 门槛的 NO-GO 结论不受单次波动影响。

