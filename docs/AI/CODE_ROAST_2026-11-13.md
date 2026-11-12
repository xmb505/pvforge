> **AI 文档（非用户文档）**：本文由 AI/代理生成，面向 AI 与开发者，可能滞后于代码。

# PVForge 代码毒舌审查（2026-11-13）

**审查范围**：`main`，HEAD `95941ed`。代码与任务给出的 665cdf9 相同，只多了一个 docs commit（95941ed）。
**已读 docs/human，本报告使用 harness read/grep 完成。**
本文不重复 `docs/AI/TECH_DEBT_AUDIT.md` 里已经写过的“双栈并存”通论；下面只写这轮清债之后**新出现或仍然很丑**的具体证据。

**一句话总结**：现在最欠骂的不是旧代码还在，而是旧代码被 `#if PVFORGE_LEGACY_SHELL` 藏起来之后就被宣布“已退休”——交付头文件仍在 include 旧 DTO，交付 UI 仍在构造旧面板，测试则靠 CMake 强行打开旧壳才活得下去，而 CI 从不运行 release 二进制。

## 〇、审查口径与范围

- 规范：已读 `docs/human/` 全部文件（`README.md`、`AI看过来，办事规范.txt`、`SPEC_TEMPLATE.md`）和 `docs/AI/README.md`。
- 工具：本报告的代码引用全部用 harness `read`/`grep` 复核，不用 shell 的 `grep`/`sed`/`cat` 查看代码；shell 只用于 `git log`、`wc` 等只读统计。
- 范围：`src/app`、`src/ui`、`src/core`、`src/model`、`src/store`、`src/framecache`、`src/htmlcache`、`src/html2`、`src/tests`、`CMakeLists.txt`、CI 和 `docs/AI/`。
- 不在范围：没有重跑整篇功能测试，没有改代码、提交 git 或创建 issue；只写这一份报告。
- 严重度：P0 = 让“测试/交付”结论失真；P1 = 现在就会产生错误行为；P2 = 会持续增加维护成本。
- 复核方式：每个 `文件:行` 都可用 harness `read` 跳到对应位置；158 个宏、16/31 个测试用例、11 个 framecache 用例等数字用 harness `grep` 复核。
- 本报告只记录当前事实和修改建议，不替人类做产品取舍。

---

## 一、P0：会让“交付质量”本身失真

### P0-1 测试构建强制打开旧壳，交付配置从未被测试

- 证据：`CMakeLists.txt:51-54` 写着 `if(PVFORGE_BUILD_SELF_TESTS AND NOT PVFORGE_LEGACY_SHELL)`，然后直接 `set(PVFORGE_LEGACY_SHELL ON)`；debug/tests/reference 三个 preset 也都显式 `PVFORGE_LEGACY_SHELL=ON`（`CMakePresets.json:18-25,48-50,62-64`）。
- 证据：CI 只在 debug 跑 ctest（`.github/workflows/ci.yml:99-102`），release 步骤只 build + 上传产物（`:128-137`），从不执行 `build/release/pvforge`。
- 为什么烂：`pvforge-tests` 链接的 `pvf_app` 和交付用的 `pvf_app` 不是同一份编译配置。测试跑的是“旧壳 + 参考壳 + 交付壳”的混合物，release 跑的是另一套代码；所谓“测试全绿”证明不了交付。
- 会怎么炸：以后任何只存在于交付配置下的启动崩溃、Qt 连接缺失、`#if` 外分支错误，CI 都看不见；用户拿到的是从未执行过的二进制。
- 怎么改：旧模型测试单独拆成 `pvforge-legacy-tests`，不再反过来强制 `PVFORGE_LEGACY_SHELL=ON`；CI 增加一条 release 冒烟（`--open` 一个 fixture + `--shot` 或 `--help`）。

### P0-2 “旧壳已退休”是假的：交付头文件仍然依赖旧 DTO

- 证据：`src/ui/Viewport.h:5`、`src/ui/TimelinePanel.h:3`、`src/ui/WorkbenchPanel.h:3`、`src/ui/TimelineData.h:4` 无条件 `#include "core/Project.h"`；`src/app/MainWindow.h:5`、`src/ui/PropertiesPanel.h:9` 无条件 `#include "html/HtmlAsset.h"`，`src/ui/PropertiesPanel.h:319` 还保留着 `html::HtmlAsset m_htmlAssetCache` 成员。
- 证据：`CMakeLists.txt:145-147` 却写 “Retired shell … None of it is reachable from the delivery entry point.”；而 `src/html/HtmlAsset.cpp`、`src/html/HtmlRenderer.cpp` 只在 `CMakeLists.txt:160-161` 的 legacy 分支里编译。
- 为什么烂：源码依赖和链接图是两回事。交付确实不链接 `core::Project.cpp`，但交付的公共头文件仍然要求 `core/Project.h` 存在，`PropertiesPanel` 甚至保留 `html::HtmlAsset m_htmlAssetCache` 成员。所谓“删掉旧模块”会直接破交付编译。
- 会怎么炸：下一个人按文档删 `src/core/Project.*` 或 `src/html`，release 编译当场爆炸；或者新代码继续自然地把旧 DTO 传进新接口，边界永久固化。
- 怎么改：交付头文件里只留 `class Project;` 前向声明，旧构造函数/旧成员全部移进 `#if` 或独立 legacy 头；`HtmlAsset` 这种纯 DTO 迁到 `model/` 或 `htmlcommon/`，不要放在待删目录。

### P0-3 158 个 `#if` 不是“清理”，是把旧代码塞进同一个函数里

- 证据：harness `grep` 在 `src/` 命中 **158** 处 `#if PVFORGE_LEGACY_SHELL`，横跨 `src/app`、`src/ui`、`src/core`。
- 证据：`src/app/MainWindow.cpp` 近 5800 行、25 个 legacy 分支；`src/ui/TimelinePanel.cpp` 27 个分支；`src/ui/PropertiesPanel.cpp` 16 个分支。
- 证据：`PropertiesPanel(EditorSession*)` 也调用同一个 `buildUi()`（`src/ui/PropertiesPanel.cpp:1010`），而 `buildUi()` 无条件构造漫画框/线条/瓷砖墙/点阵/旧 HTML 面板（`:1128` 附近）；到了 session 刷新时才统一 `hide()`（`:4689-4693`）。交付在构造一堆永远不会显示、回调全被 `#if` 掏空的旧控件。
- 为什么烂：这不是模块边界，而是编译期开关。读一个函数要同时在脑子里跑两套预处理结果；改共享初始化代码时，debug（legacy ON）能过，release（legacy OFF）未必。
- 会怎么炸：一次只改 debug 的提交，把 release 路径打断；或者某个旧控件在 delivery 里仍被 `new` 出来，带着空 lambda 参与信号连接，内存和启动时间白付。
- 怎么改：旧文件整体移出交付源列表，放到独立 `legacy` target；`MainWindow`/`TimelinePanel`/`PropertiesPanel` 的交付实现只保留 session 路径，不要在一个函数里 `#if` 两套 UI。

---

## 二、P1：现在就在制造错误行为

### P1-1 交付版 main.cpp 用 `-test` 后缀判断“测试模式”，会劫持真实工程路径

- 证据：`src/main.cpp:36-57` 在**没有** `#ifdef PVFORGE_SELF_TESTS` 的情况下扫描所有参数：`argument.endsWith("-test")` 就重定向 QSettings 到临时目录并设置 `XDG_CONFIG_HOME`。
- 证据：同样的后缀判断还复制在 `src/app/WelcomeWindow.cpp:34` 和 `src/app/MainWindow.cpp:2354`，分别用来关闭首次引导。
- 为什么烂：`pvforge --open /home/user/my-pv-test` 是合法路径。release 二进制会因此使用临时配置目录、丢掉用户的语言/布局/MCP 设置，并且不再显示 onboarding；用户只会觉得“设置怎么全没了”。
- 会怎么炸：任何以 `-test` 结尾的目录（`project-test`、`demo-test`）都会触发；MCP endpoint/token 也会写到临时目录，外部客户端连不上。
- 怎么改：测试开关只认精确的 `--xxx-test` 集合（或直接复用 `Registry`），检测逻辑统一到一处，并且放在 `#ifdef PVFORGE_SELF_TESTS` 里。

### P1-2 Migrations 目前是摆样子，而且只能迁移 project.json

- 证据：`src/model/Migrations.cpp:8-13` 的 production registry 是 `static const QVector<Migration> steps;`，空链；所有迁移逻辑只被测试用的“注入链”覆盖。
- 证据：`src/store/ProjectStore.cpp:297-312` 只对 `project.json` 调 `migrations::upgrade()`；随后 `src/store/ProjectStore.cpp:330-353` 直接对每个 `entities/*.json` 调 `entityFromJson()`，没有任何版本检查或迁移。
- 证据：`src/model/Migrations.h:46-49` 的 `minimumSupportedVersion()` 在 production 没有调用者，只有 `src/model/tests/model_tests.cpp:2009` 断言它等于当前版本。
- 为什么烂：真正会随格式演化的是实体/内容/关键帧文件，而迁移框架只覆盖壳工程；一旦 `kProjectFormatVersion` 从 1 升到 2，`project.json` 能升级，`entities/*.json` 会在严格解析里直接失败。
- 会怎么炸：第一次格式升级后，所有旧工程都能“打开一半”，然后在实体文件上报“缺少必填字段”；测试仍然绿，因为测试的是不存在的合成链。
- 怎么改：要么给 entity 文件加独立 version 并纳入同一条迁移链，要么删掉这套空框架，等有真实 v2 数据时再写；测试必须用真实旧工程 fixture 走一遍。

### P1-3 同一个“缓存占用”有两套口径，设置页显示的数字不可信

- 证据：`src/app/MainWindow.cpp:106-126` 的 `scanCacheUsage()` 对目录下**所有文件**累加 bytes，但 frames 只数 `.qoi/.webp`；`index.sqlite`、`-wal`、`-shm`、meta 文件都算进“缓存占用”。
- 证据：`src/framecache/Store.cpp:388,771-778` 的 `diskBytes` 只统计已索引帧文件的 bytes；`src/workbench/Cache.cpp` 的 stats 又把它转成 `CacheStats`。
- 证据：设置页和清空提示用的都是 `scanCacheUsage()`（`src/app/MainWindow.cpp:2113,2119,2131-2141`），而不是 cache 服务的 stats。
- 为什么烂：同一次打开设置页，“帧数”和“MB”不是同一个集合；用户会看到 1 帧占 1.3 MB（其中 1.2 MB 是 SQLite index），也会看到清空后字节数对不上。
- 会怎么炸：用户以为缓存坏了；支持成本上升；以后想加“按帧平均大小”或容量告警，会基于错误口径继续算。
- 怎么改：只保留一个 `CacheStats` 公共结构（磁盘帧字节、索引字节、帧数分开），设置页/状态栏/清空提示全部调用同一个 API。

### P1-4 “清空工作台缓存”只清当前相机，失败也不报

- 证据：`src/next/app/WorkbenchServices.cpp:564-572` 的 `clearCache()` 只调 `m_cache->clear()`，而 `m_cache` 是当前相机/当前签名目录的实例。
- 证据：`src/app/MainWindow.cpp:2133-2136` 只有在 `cacheDirectory()` 为空时才 `QDir(workbenchRoot).removeRecursively()`；有 cache 实例时只走 `clearCache()`，**且 `removeRecursively()` 的返回值被忽略**。
- 证据：`src/app/MainWindow.cpp:2142-2146` 无论如何都弹“已清空工作台缓存：N 帧，释放 X MB”。
- 为什么烂：工作台根目录下按 `directorySignature()` 和相机 key 分目录（`src/workbench/Cache.cpp:141-145`）；切换过相机/清晰度后，旧目录不会被清。权限失败时 UI 仍然报成功。
- 会怎么炸：用户点了三次“清空”，磁盘还是满的；日志里没有任何失败信息；测试也不会发现，因为测试只建一个相机。
- 怎么改：清空操作放到 `WorkbenchServices`，删整个 `m_cacheRoot` 并返回成功/失败和失败路径；UI 只根据真实结果提示。

### P1-5 `TestSupport.h` 是垃圾场，测试仍是巨型复制粘贴

- 证据：`src/tests/TestSupport.h` 189 行里塞了 80+ 个 include（`:12-83`），包括旧壳的 `core/Project.h`（`:41`）和待删目录的 `html/HtmlCaptureTest.h`（`:39`）；`ui/WorkbenchPanel.h` 在 `:67` 和 `:83` 重复 include。
- 证据：13 个测试 TU 全部只 `#include "TestSupport.h"`，任何测试都隐式依赖整个应用头文件集合；测试源码约 2.5 万行，harness grep 至少命中 100 个 `registry.add`（grep 上限截断）。
- 证据：单函数巨大：`PropertiesTests.cpp:1864` 的 `specialTest` 一直写到 `:2647` 的注册函数；`TimelineTests.cpp:286` 的 `timelineEditTest` 到 `:909`；`McpTests.cpp:1202` 的 `mcpWriteTest` 到 `:2338`。
- 为什么烂：这不是共享测试工具，是把整个 app 的头文件当预编译头；任何一个模块改头文件，所有测试重编。巨型测试函数里几百次 `processEvents` 轮询，失败时无法定位是哪一步。
- 会怎么炸：构建时间越来越长；测试无法脱离旧壳编译；一个断言失败只能靠打印日志猜。
- 怎么改：模块测试自带最小 include；`TestSupport` 只留通用等待/HTTP helper；把 700 行级测试函数按行为拆成多个 `CHECK` 段或 QTest 用例。

### P1-6 CI 在为绿而绿

- 证据：`.github/workflows/ci.yml:24` 把 `viewport.tests`、`next.media_services`、`next.camera_monitor` 整体列为 known flaky；`:102` 对全部测试 `--repeat until-pass:2`；`:113-116` 这三组再用 `continue-on-error: true` 单独跑。
- 证据：文档指标 job 也是 `continue-on-error: true`（`:32-41`），所以 `STATUS.md` 的数字漂移永远不会判红。
- 为什么烂：`viewport.tests` 不是边角测试，它覆盖视口核心行为；把它整体排除，等于宣布“视口回归不阻塞合并”。`until-pass:2` 让一次真失败只要第二次碰巧过就变绿，非确定性 bug 被系统性隐藏。
- 会怎么炸：视口/相机/媒体服务回归悄悄进 main；等用户发现时已经跨了几个版本。文档数字继续漂移，AI 继续按错的 STATUS 干活。
- 怎么改：只对具体 flaky 用例做 `--repeat`，不要整组 exclude；`next.media_services` 的端口冲突应改成 bind 0；viewport 必须阻塞；doc-metrics 至少对指标区块做阻塞校验。

---

## 三、P2：不致命，但会持续收利息

### P2-1 两套 HTML 渲染协议并存，旧的那套还被测试养着

- 证据：旧 `src/html/HtmlRenderer.cpp` 用 `pvf.ready` / `pvf.rendered`（`:839,965,969`）；新 `src/html2/PvhtmlRenderer.h:54-60` 用 `__PVFORGE__` / `pvforgeframe` / `__PVFORGE_READY__`。
- 证据：`CMakeLists.txt:226-228` 仍把 `src/html/HtmlCaptureTest.cpp`、`src/html/HtmlLayerTest.cpp` 编进测试目标。
- 为什么烂：传输层已经搬到 `htmlcommon`，但渲染脚本、等待逻辑、变量注入还是两份；旧测试文件又让 `src/html` 无法真正删除。
- 会怎么炸：MCP 客户端/文档作者按旧协议写页面，在交付的 html2 里不工作；修一个捕获 bug 要改两处。
- 怎么改：删旧 `HtmlRenderer` 和它的测试，只保留 `PvhtmlRenderer` 一份协议并写进文档。

### P2-2 `HtmlFrameService` 明确泄漏 renderer，并且有一处无锁读

- 证据：`src/html2/HtmlFrameService.cpp:707-716` 发现 final renderer 在别的线程时，直接 `m_final = nullptr;`，注释自己写 “leaking the renderer”。Chrome 进程和它的 QObject 子对象都不再有人回收。
- 证据：`src/html2/HtmlFrameService.cpp:496` 在锁外读 `m_stop`；写侧 `requestStop()` 在锁内，属于数据竞争。
- 为什么烂：把“跨线程删除危险”处理成“泄漏”，等于把生命周期问题变成磁盘/内存问题；无锁读是 UB。
- 会怎么炸：导出线程换线程/重试时每轮泄漏一个 Chromium renderer；TSAN 或极端调度下出现随机崩溃。
- 怎么改：用 `QMetaObject::invokeMethod(ownerThread, ...)` 投递 `stop+delete`，至少让 owner 线程回收；`m_stop` 读取放进 `QMutexLocker`。

### P2-3 MCP 内部异常被吞成一句 “Internal error”

- 证据：`src/mcp2/HttpServer.cpp:1234-1241`：`catch (const std::exception&)` 和 `catch (...)` 都只发 `"Internal error"`，不记录 `what()`、方法名或请求 id。
- 为什么烂：MCP 是 AI 的工作入口；工具调用崩了却没有任何日志线索，用户只能看到“内部错误”。
- 会怎么炸：线上问题无法复现、无法定位；只能靠加日志重新构建。
- 怎么改：catch 里 `qWarning()` 记录 `what()` 和 `method`/request id，再返回统一的通用错误给客户端。

### P2-4 i18n 是半套：交付 UI 大量硬编码中文，参考壳反而用 `tr()`

- 证据：`Lang::t()` 全仓只有 77 处；仅 `src/app` 下硬编码中文的 `QStringLiteral` 就至少 500 处（harness grep 在 500 截断）。
- 证据：`src/app/MainWindow.cpp` 没有一处 `tr()`，而参考壳 `src/next/app/NextWindow.cpp:137,145` 使用 `NextWindow::tr("打开")`；新增的 `src/model/Migrations.cpp:30,36,58` 也是硬编码中文错误。
- 为什么烂：语言包只覆盖菜单/少量标签，错误对话框、设置页、导出页仍然是中文；`Lang::t(key, fallback)` 还把中文 fallback 复制进代码，翻译时两边都要改。
- 会怎么炸：英文用户看到中英混排；切换语言后错误信息不翻译；后续每加一个功能继续二选一。
- 怎么改：二选一——要么统一走 `Lang::t`（key 唯一，中文放语言包），要么明确只做中文并删掉半套 `Lang`；不要维持现在这种双轨。

### P2-5 共享 `framecache` 抽到一半：Store 成了知道所有历史格式的上帝对象

- 证据：`src/framecache/Store.h:44-65` 的 `Options` 同时带 `extensions`、`legacyTables`、`legacyJsonIndex`、`validate` 回调；`src/framecache/Store.cpp:177-263` 同时认识 workbench 的 `frames` 旧表和 htmlcache 的 `entries` 旧表、JSON sidecar。
- 证据：`src/htmlcache/DiskCache.cpp:173-176` 的 `indexBackend()` 只可能返回 `"SQLite"`，`src/htmlcache/tests/htmlcache_tests.cpp:446` 还在断言这个常量；这是删掉 JSON 后端后遗留的死抽象。
- 证据：`src/framecache/Store.cpp:154` 建了 `frame_entries_access(last_access)` 索引，但 `:470-490` 的 `enforceBudget()` 仍然在 QHash 上做 O(n) 扫描，逐条淘汰。
- 为什么烂：公共层为了两个消费者长了 4 个可选旋钮和两套历史 schema；新缓存类型要改这里。死 API + 死测试让“通过”变成噪声。索引建了不用，预算缩小时是 O(n²)。
- 会怎么炸：缓存目录很大时改小磁盘上限/启动清理会卡 UI；以后第三种缓存（video2）想复用，先要理解 workbench 和 htmlcache 的历史。
- 怎么改：`Store` 只留 put/get/remove/LRU 核心，历史导入拆成独立 `LegacyImport`；删 `indexBackend()` 及其断言；淘汰用 `DELETE ... ORDER BY last_access LIMIT` 或有序结构。

### P2-6 `docs/AI` 的数字和边界仍然自相矛盾，而且 CI 不拦

- 证据：`docs/AI/STATUS.md:10` 的生成 HEAD 是 `c147b55`，当前是 `95941ed`；`:148` 写 htmlcache 13 测试，实际 `src/htmlcache/CMakeLists.txt:45-62` 是 16 个；`:152` 写 workbench 22，实际 `src/workbench/CMakeLists.txt:54-85` 是 31 个；新模块 `src/framecache`（11 个用例）在模块表里根本没有条目。
- 证据：`docs/AI/IMPLEMENTATION_CONTRACT.md:26` 要求“不要 include 旧 `core/Project.h`”，但交付头文件 `src/ui/Viewport.h:5` 等无条件 include（见 P0-2）。
- 证据：`docs/AI/ROADMAP.md:61` 和 `docs/AI/EXTENDING.md:98` 还说 CDP/browser 在 `src/html`，但 `src/html2/CMakeLists.txt:19-33` 已经改从 `htmlcommon` 编译。
- 为什么烂：STATUS 的数字被做成“生成区块”后，旁边的手写模块表反而没人维护；CI 的 doc-metrics 又是 advisory，漂移不会有人管。边界文档和代码直接冲突。
- 会怎么炸：下一个 agent 按 IMPLEMENTATION_CONTRACT 拒绝 include `core/Project.h`，却发现交付头文件已经这么干；或者按 ROADMAP 去 `src/html` 找 CDP，找不到。
- 怎么改：模块表也纳入 `doc-metrics.sh` 或删掉手写数字；修掉三条矛盾描述；把指标区块的 `--check` 改成阻塞。

### P2-7 HTML 缓存清空同样不看返回值，失败仍报成功

- 证据：`src/app/MainWindow.cpp:1976` 调 `QDir(path).removeRecursively()`，返回值直接丢弃；紧接着 `:1978-1982` 无条件显示“已清理 HTML 缓存：N 个文件，X MB”。
- 为什么烂：和 P1-4 是同一类错误：先数文件，再删除，不检查删除是否成功，最后按“删除前的大小”报成绩。
- 会怎么炸：缓存目录权限不足、文件被 Chrome 占用时，用户看到“已清理”，实际一个字节没释放；下次打开设置页数字原样回来。
- 怎么改：删除后重新扫描并用真实差值报数；`removeRecursively()` 失败时列出失败路径，不要报成功。

---

## 四、哪些改动是白忙/过度设计

1. **空迁移链**：`Migrations.cpp` 78 行 + 一整套单测，production registry 为 0 步，且只能迁移 `project.json`。这是为“未来格式”先写了一个未来用不上的保险，还给了假安全感。
2. **`minimumSupportedVersion()`**：production 零调用，只有测试断言它等于当前版本。要么让它真的参与打开工程，要么删。
3. **`indexBackend()`**：JSON 后端删掉后，这个函数只可能返回 `"SQLite"`，测试还专门断言它。属于“为了保留旧接口而保留”。
4. **`Store::Options` 的万能化**：共享缓存本来是正确方向，但把两家的 legacy table、JSON sidecar、validator 全塞进一个 Options，等于把“重复实现”换成“共享 God object”。拆分比合并更值钱。
5. **用 `#if` 代替删除**：158 个宏分支没有减少一行旧逻辑，只是让 grep 更难、review 更贵。旧代码该在独立 target 里，而不是藏在交付函数中间。
6. **`TestSupport.h` 的“共享 include 集”**：把 80 个头文件塞给所有测试，不是复用，是把编译依赖全局化。真正的共享应该只有等待、临时目录、HTTP 这类小工具。
7. **三份 `-test` 后缀判断**：`src/main.cpp:38`、`src/app/WelcomeWindow.cpp:34`、`src/app/MainWindow.cpp:2354` 各写一遍。这既不是测试框架，也不是产品行为，是复制粘贴出来的临时开关。
8. **`scanCacheUsage()` 的第二套统计口径**：为了“设置页不打开缓存索引”又写了一套目录扫描（`src/app/MainWindow.cpp:110-127`），结果和 `framecache::Store` 的 stats 长期对不上。省下的那次打开，换来两套永远需要同步的数字。

---

## 五、哪些地方其实做得不错，别改坏

1. **`main.cpp` 真的瘦下来了**：126 行，只做 QApplication、`--open/--shot`、welcome；“未知 `--*-test` 直接失败”而不是掉进欢迎窗（`src/tests/TestRegistry.cpp:45-57`）是个好设计。
2. **extras 保留是实的，不是摆样子**：`Project/Entity/Content/Keyframe` 都在 `toJson` 里 `mergeExtras`、在 `fromJson` 里 `extrasFrom`（`src/model/Entity.cpp:179,331`；`src/model/Content.cpp:507,675`；`src/model/Project.cpp:368,465`），且 `mergeExtras` 保证已知字段优先。这个可以放心依赖。
3. **`framecache::Store` 核心是靠谱的**：QSaveFile 原子写、WAL、启动时校验文件存在/大小、孤儿清扫、旧表/旧 JSON 导入、重启后索引仍在，测试也覆盖了这些路径（`src/framecache/Store.cpp:103-107,330-417`；`src/framecache/CMakeLists.txt:31-43`）。
4. **`Cache::encode()` 与 `storeEncoded()` 的线程边界是对的**：编码是纯函数可进后台池，SQLite 留在 owner 线程（`src/workbench/Cache.h:87-97`）。这是少数“复杂但理由成立”的地方。
5. **交付/参考/测试 target 的拆分方向正确**：`PVFORGE_BUILD_REFERENCE` 默认 OFF、release 只产出 `pvforge`/`pvforge-mcp`，测试专用 `.cpp` 也移出了 `pvforge`（`CMakeLists.txt:208-236`）。方向别退回去，只需要把 legacy 也按同样方式彻底拆走。
6. **格式版本拒绝写盘**：`ProjectStore` 在打开阶段只读，遇到更高版本直接报错且不动原文件（`src/store/ProjectStore.cpp:278-312`）。这个保护要保留。

---

## 六、如果只能修 3 条，修哪 3 条

1. **先恢复测试信号**：去掉 `PVFORGE_BUILD_SELF_TESTS → PVFORGE_LEGACY_SHELL=ON` 的强制，把旧模型测试拆成独立 target，CI 增加 release 二进制冒烟。否则后面所有“测试通过”都不可信。
2. **再把旧壳从交付头文件/构造路径里真正拔掉**：前向声明 `Project`，旧构造函数和旧成员移进 legacy 头，`PropertiesPanel`/`MainWindow` 不再在交付里 `new` 旧面板。否则 P0-2/P0-3 会一直复发。
3. **修 `-test` 后缀劫持**：交付版只认精确的已知开关，检测逻辑收口到一处并放进 `PVFORGE_SELF_TESTS`。这是一天内能完成的用户可见 bug，优先级高于继续加功能。

紧接着的第 4 条：把缓存统计和清空做成一个说真话的 API（P1-3/P1-4），它直接影响用户磁盘和信任。

---

*审查完成。没有改任何代码；下一次复审请重新对 HEAD 取数，不要引用本报告的 HEAD。*
