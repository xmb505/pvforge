> **AI 文档（非用户文档）**：本文由 AI/代理生成，面向 AI 与开发者，可能滞后于代码。用户请以 `README.md`、`docs/USAGE.md`、`docs/FEATURES.md` 为准。

# PVForge 技术债审计（2026-11-12）

> 审计对象：`/home/xmb505/pvforge`，分支 `main`，HEAD `7215da8`。关联 issue：#82。
> 方法：先读 `docs/AI/STATUS.md`、`ENTITY_MODEL.md`、`IMPLEMENTATION_CONTRACT.md`、`ROADMAP.md`、`INTEGRATION_PLAN.md`、`UI_BACKEND_INTEGRATION.md`，再**以代码和实测数字为准**逐项核对。
> 口径：所有数字由 `wc -l`、`grep`、`ctest -N`、`strings`、`readelf`、`git ls-files` 实测；未复现的结论标注「未验证」。本报告不改代码，只记债。

---

## 0. 执行摘要

1. **交付二进制里同时存在两套工程模型、两套 UI、两套后端。** `src/core`（9,332 行）与 `src/model`+`src/store`（8,458 行）都链进 `pvforge`；`src/ui`（29,152 行）与 `src/next`（41,531 行）并存；`render/html/audio/mcp` 与 `render2/html2/audio2/mcp2` 各自成对。见 `CMakeLists.txt:43-146`、`:148-176`。
2. **交付 target 里约 1.23 万行旧实现只在旧构造函数/旧自测路径可达。** 旧 `MainWindow` 构造函数（`src/app/MainWindow.cpp:372`）在交付启动路径中不再实例化，但它仍编译，并拖着旧 `Project`/`History`/`Commands`/`CheckpointManager`/`SceneRenderer`/`HtmlRenderer`/`McpServer`/`McpCommandService`/`AudioEngine` 一起进二进制。见 §3.2。
3. **参考壳默认参与构建但不交付。** `src/next` 非测试代码 26,484 行，其中 17,219 行被编进静态库、却没有任何交付链接边；`src/next/app` 的 4 个参考窗口 + 6 个测试可执行文件默认全部构建。见 `CMakeLists.txt:171`、`src/next/app/CMakeLists.txt:145`。
4. **`src/main.cpp` 26,953 行，只有约 245 行生产逻辑（0.9%）。** 其余是 26,476 行测试函数（`145-26620`）和 232 行 `--*-test` 分发（`26663-26894`）。Release 二进制里仍能扫出全部 **101** 个测试开关。见 §3.3。
5. **没有 CI，也没有任何构建/格式/静态检查配置。** 无 `.github`、`CMakePresets.json`、`Dockerfile`、`.clang-format`、`.clang-tidy`、`.editorconfig`；`CMAKE_CXX_FLAGS` 为空，无 `-Wall`、无 sanitizer。见 §3.7。
6. **工程格式有两个互不兼容的版本号。** 旧模型 `kProjectVersion = 4`（`src/core/Project.h:26`），新模型 `kProjectFormatVersion = 1`（`src/model/Project.h:14`），文件都用 `format`+`version` 键但取值不同；新模型版本严格相等、没有迁移框架。见 §3.4。
7. **错误与 i18n 是系统性欠账。** 生产代码 4,705 处中文字面量、93 个文件（含测试共 5,204 处 / 114 个文件）；`Lang::t` 只有 77 次调用，语言包各 49 个 key；后端 762 条中文错误串、209 个 `QString* error` 出参，只有 `mcp2` 有结构化 code。UI 甚至用错误串做分支（`ExportDialog.cpp:45`）。见 §3.6。
8. **测试本身已是两套体系。** 330 个 CTest + 101 个 `--*-test`；最近一次完整跑（`build-coord-tests`，2026-11-12 22:22）有 3 个失败：`viewport.tests`、`next.media_services`、`next.camera_monitor`。总耗时约 74 秒，说明不是「跑不动」，而是「没人跑」。见 §3.7。

---

## 1. 度量总览

| 指标 | 实测 | 说明 |
|---|---:|---|
| 生产代码（非测试 `.cpp/.h`） | 149,117 行 | `git ls-files 'src/*.cpp' 'src/*.h' \| grep -vi test \| xargs wc -l` |
| 测试代码 | 36,108 行 / 37 文件 | `.cpp/.h`；含 7 个测试 CMakeLists 共 36,362 行 / 44 文件 |
| 源码合计 | 187,119 行 / 349 文件 | 318 个 `.cpp/.h`（185,225 行）+ 31 个 CMake 文件（1,894 行） |
| `src/main.cpp` | 26,953 行 | 占非测试 `.cpp/.h` 18.1% |
| 旧模型 `src/core` | 9,332 行 / 30 文件 | 含 2,961 行 `Project.cpp` |
| 新模型 `src/model` | 4,724 行 / 18 文件 | 生产 3,017 行 |
| `src/store` | 3,734 行 / 4 文件 | 生产 1,944 行 |
| 旧 UI `src/ui` + `src/app` | 42,581 行 / 59 文件 | `ui` 29,152 + `app` 13,429 |
| 参考壳 `src/next` | 41,531 行 / 73 文件 | 非测试 `.cpp/.h` 26,484 行；未链接 17,219 行 |
| 旧后端 `render/html/audio/mcp` | 12,107 行 | 4 个目录合计 |
| 新后端 `render2/html2/htmlcache/audio2/mcp2` | 24,247 行 | 5 个目录合计（含测试） |
| CMake 文件 / `project()` / 静态库 / 可执行文件 | 32 / 25 / 27 / 33 | 33 = 2 个产品 + 31 个测试 |
| CTest | 330 | `build-coord-tests`；旧 `build` 为 327 |
| `--*-test` 开关 | 101 | `grep -oE '"--[a-z0-9-]+-test"' src/main.cpp` |
| 中文字面量 / `Lang::t` / 语言 key | 4,705 / 77 / 49 | 生产 93 个文件；含测试共 5,204 处 / 114 文件 |
| `QString* error` 头文件签名 | 209 | 出参式错误 |
| 真实 TODO/FIXME/HACK | 3 | 其余 `XXXXXX` 是临时目录模板 |
| 文档 | 31 篇 / 13,067 行 | `docs/AI` 29 篇 + `docs/` 2 篇用户文档 |
| Release 直接/传递动态依赖 | 15 / 177 | `readelf` / `ldd` |
| Release 中测试开关 | 101 | `strings -e l dist/pvforge` |

---

## 2. 债务清单

严重度：P0 = 阻塞交付或持续制造回归；P1 = 明显拖慢迭代/有数据或行为风险；P2 = 可排期清理。
工作量按单人日估算，含自测，不含大规模重构后的回归。

| 编号 | 领域 | 证据（路径:行 / 命令） | 严重度 | 影响 | 工作量 | 关联 |
|---|---|---|---|---|---|---|
| D01 | 双工程模型 | `CMakeLists.txt:45` 编 `core/Project.cpp`；`:175` 链 `pvf_next_document`；`src/core/Project.h:26` vs `src/model/Project.h:14` | P0 | 两个 `Project`/`Entity`/`Easing` 语义并存，改一处忘另一处；新人无法判断权威数据层 | 3-5d | #75 |
| D02 | 交付含不可达旧实现 | 旧 `MainWindow` ctor `src/app/MainWindow.cpp:372` 交付不实例化；11 个旧构造函数；逐文件合计 12,349 行 | P0 | 编译时间、二进制体积、审查噪音；任何误接线即空指针/旧格式写入 | 3-5d | #75 #77 |
| D03 | `main.cpp` 测试巨兽 | `wc -l src/main.cpp` = 26,953；测试函数 `145-26620`；分发 `26663-26894`；生产约 245 行 | P0 | 单文件无法 review；改一行触发全量重编；测试与产品同生命周期 | 5-7d | #76 |
| D04 | Release 含测试入口 | `strings -e l dist/pvforge \| grep -cE '^--[a-z-]+-test$'` = 101；`nm` 可见 28 个测试符号 | P0 | 发布物带测试后门与 2.6 万行测试代码；`BUILD_TESTING=OFF` 只关测试 target，关不掉这些代码 | 含 D03 | #76 |
| D05 | 参考壳默认编译 | `CMakeLists.txt:171`；`src/next/app/CMakeLists.txt:145`；未链接非测试代码 17,219 行 | P1 | 每次全量构建多编一整套不交付 UI；CI 时间与磁盘被浪费 | 1-2d | #77 |
| D06 | 双 UI 栈 | `ui` 29,152 + `app` 13,429 vs `next` 41,531；同名 8 对文件 | P1 | 同一功能两套交互实现，修 bug 要判断「改哪套」；参考壳逐渐腐烂 | 长期 | 新 |
| D07 | 双后端模块 | `render` 553 / `render2` 3,365；`html` 4,780 / `html2` 4,097；`mcp` 6,506 / `mcp2` 13,288；`audio` 268 / `audio2` 1,783 | P1 | 概念重复；旧模块仅服务旧 ctor/测试，却占编译与链接 | 8-15d | #77 |
| D08 | 同名双实现 | 8 对同名 `.cpp`（`Project`、`Easing`、`ExportDialog`、`SettingsDialog`、`McpLogPanel`、`PropertiesPanel`、`TextLayoutDialog`、`WorkbenchPanel`）合计 20,395 行 | P1 | 20k 行里一半是交付、一半是参考，搜索/重构命中率减半 | 8-12d | 新 |
| D09 | 依赖方向倒置 | `core/Project.cpp:3-6` → `html/`、`media/`；`render2/Render.cpp:3-5` → `core/`；`ui` → `app` → `next` → `core/html` | P1 | 分层失效，无法单独复用「文档层」；改动扩散不可预测 | 5-8d | 新 |
| D10 | Easing 双份 | `core/Easing.cpp` 327 行；`model/Easing.cpp` 328 行；diff 380 行 | P2 | 30 个缓动预设两份实现，新增预设要改两处 | 0.5-1d | 新 |
| D11 | 无 CI | 根目录无 `.github`；`ls -a` 无任何 CI 配置 | P0 | 330 个测试与 3 个已知失败无人守门；每次「本地能过」不可复现 | 2-3d | #78 |
| D12 | 无构建预设/依赖锁定 | 无 `CMakePresets.json`、`Dockerfile`、`vcpkg.json`、`conanfile.txt` | P1 | 环境差异靠口头；Release 只能在作者机器复现 | 1-2d | #78 |
| D13 | 最低 Qt 与 LTS 发行版冲突 | `CMakeLists.txt:16` 要求 Qt 6.5；README:38 宣称 6.5+；Ubuntu 24.04 只有 6.4.2（据 #78，未本机复现） | P1 | 大量用户无法从源码构建；错误信息是 CMake 版本不满足 | 1-2d | #78 |
| D14 | 无编译告警/静态检查 | `CMakeLists.txt` 无 `-Wall/-Wextra/-Werror`；无 `.clang-format/.clang-tidy`；Cache 中 `CMAKE_CXX_FLAGS` 为空 | P1 | 类型截断、未初始化、悬空引用只能靠人眼 | 0.5-1d | 新 |
| D15 | 发布物动态依赖 | `readelf -d dist/pvforge` 15 个直接依赖；`ldd` 177 行；未 strip | P2 | 只能跑在 Qt 6.10/FFmpeg 8 的系统；新手第一步就失败 | 2-3d | #78 |
| D16 | 工程格式双版本且无迁移 | `kProjectVersion=4`；`kProjectFormatVersion=1`；`ProjectStore.cpp:264` 严格相等；旧模型 `Project.cpp:2520` 接受 1..4 | P0 | 新模型一旦升版本，所有既有工程直接打不开；两套 format 值易误判 | 3-5d | #75 #82 |
| D17 | 解析静默默认/丢未知字段 | `model/Entity.cpp:166-181` 缺失字段取默认；`toJson` 只写已知字段 | P1 | 文件损坏/未来字段会被静默改成默认值并在下次保存时抹掉 | 2-3d | 新 |
| D18 | 提交消息契约失效 | `ProjectDocument.cpp:19` 固定 `"pvf: human edit"`；`:182` 等 5 处 `Q_UNUSED(message)`；undo 按 1s debounce 合并 | P2 | Git 历史无法分辨操作；Ctrl+Z 可能一次退多个手势 | 1-2d | 新 |
| D19 | 双模式 MainWindow 无类型约束 | `m_project` 327 处引用，仅 4 处空指针守卫；旧 ctor 372 / 新 ctor 790 | P2 | 当前靠调用路径纪律避免崩溃；新增共享方法极易踩空 | 3-5d | 新 |
| D20 | QThread 手工所有权 | 5 个 `QThread` 子类（`html2/HtmlFrameService.cpp:50,200,361`、`video2/VideoExtractionQueue.cpp:100`、`workbench/PrewarmService.cpp:38`）；手工 `new/delete` + `wait()` | P2 | 局部都写了 `wait()`，但契约靠注释；漏一处即 UAF/泄漏 | 3-5d | #80 |
| D21 | 导出线程跨线程用 UI 服务 | `MainWindow.cpp:3625` 在 `QtConcurrent::run` 中调用 `MediaServices`；STATUS.md:168 自认「导出期间未阻止 MCP 写操作」 | P1 | `MediaServices`/`Document` 属 UI 线程，无锁读工程，MCP 并发写时数据竞争 | 1-2d | 新 |
| D22 | 轮询等待事件循环 | `WorkbenchServices.cpp:392-401` `processEvents + msleep(1)`；注释自认仅测试/profile | P2 | 测试不稳定；若误用于生产会重入 UI | 0.5d | #79 |
| D23 | 生产代码 `processEvents` | `EditorSession.cpp:164`（MCP 批处理）、`MainWindow.cpp:3537`（导出进度）、`WorkbenchServices.cpp:398` | P2 | 重入风险；调用栈中任何对象都可能被删除 | 1-2d | #79 |
| D24 | 中文硬编码 / i18n 假切换 | 生产 4,705 处中文、93 文件（含测试 5,204/114）；`Lang::t` 77 次；`assets/lang/*.json` 各 49 key | P1 | 切换 English 后 90% 界面仍是中文；API 错误无法本地化 | 5-8d | #81 |
| D25 | 错误模型缺失 | 209 个 `QString* error`；后端 762 条中文错误串；结构化 code 只在 `mcp2` | P1 | 调用方只能比字符串；同一失败在不同层文案不同 | 5-8d | #81 |
| D26 | 错误串当 API | `ExportDialog.cpp:45-57` 比较 `"output_exists"`、`"encoder_unavailable"`；`Export.cpp:217` 中文错误与 `:567` 英文 code 混用 | P1 | 改文案即改行为；UI 无法区分「可重试」与「参数错」 | 1-2d | 新 |
| D27 | 已知测试失败 | `build-coord-tests/Testing/Temporary/LastTestsFailed.log`：`viewport.tests`、`next.media_services`、`next.camera_monitor`（330 中 3 失败，2026-11-12 22:22） | P1 | 「测试红了」成为常态，真回归被淹没 | 1-2d | #72 #73 #74 |
| D28 | 双测试体系 | 330 CTest + 101 个 `--*-test` + 119 个测试函数 | P1 | 同一功能两套断言；新人不知道跑哪套；CI 无法统一 | 3-4d | #76 |
| D29 | 测试默认全量构建 | 31 个测试可执行文件；`include(CTest)` 顶层默认开 | P1 | 只想跑应用也要编全部测试；拉长反馈环 | 含 D03 | #76 |
| D30 | 缓存基础设施重复 | `workbench/Cache.cpp`（751 行，SQLite+LRU+WebP/QOI）；`htmlcache/DiskCache.cpp`（SQLite+LRU+WebP）；两套索引/淘汰 | P2 | 两处 LRU/事务/损坏恢复各修一遍；预算与统计口径分裂 | 3-5d | 新 |
| D31 | 死代码 PrewarmService | `workbench/PrewarmService.cpp` 317 + `.h` 90 行；生产无引用 | P2 | 读代码时误以为有后台预热；维护两套扫描语义 | 0.5d | 新 |
| D32 | STATUS 数字全面过时 | `docs/AI/STATUS.md:7` 最新提交 `491c270`（实际 `7215da8`，其后 55 个提交）；`:229` 268 项（实际 330）；`:205` 9.5MB（实际 11MB） | P1 | 交接时被错误数字误导；审计/排期基于假基线 | 1d | #82 |
| D33 | 发布说明版本错 | `dist/RUN.txt:1` 写 0.2.0；`CMakeLists.txt:3` 与 README 是 0.3.0 | P2 | 用户/支持无法判断版本 | 0.1d | 新 |
| D34 | 文档无生成/校验 | 31 篇 13,067 行；`UI_BACKEND_INTEGRATION.md` 引用的 `MainWindow.cpp:638` 已非所述代码；计划文档多处「已取消」套「历史计划」 | P2 | 文档越长越不可信；读者需二次核对代码 | 2-3d | #82 |
| D35 | 模块 CMake 样板重复 | 25 个 `project()`、31 个 `find_package(Qt6`、50 个 `set(CMAKE_CXX_STANDARD` | P2 | 版本/标准升级要改 25-50 处；但也支撑独立构建，属可接受冗余 | 1-2d | 新 |
| D36 | 旧代码无删除策略 | 183 处 legacy/旧壳/兼容注释；`docs/AI/ROADMAP.md:57` 明确「旧壳不退休」 | P2 | 「先保留」没有退出条件，双栈会固化 | 决策项 | #77 |

---

## 3. 专题分析

### 3.1 双模型 / 双栈：不是「迁移中」，而是「长期并存」

| 层 | 旧 | 新 | 交付实际使用 |
|---|---|---|---|
| 工程数据 | `src/core/Project.h`（2,961 行 `Project.cpp`，`EntityId=int`，`QVariantMap data`） | `src/model`（4,724 行，`Uuid`，`Content` 强类型，实体拆文件） | 新模型；旧模型仍编译并作为时间轴 DTO |
| 工程落盘 | 单文件 `.pvproj`，`kProjectVersion=4` | `.pvproj/` 目录，`project.json`+`entities/*.json`，`kProjectFormatVersion=1` | 新 Store |
| UI | `src/app`+`src/ui` 42,581 行 | `src/next` 41,531 行 | 旧壳 `src/app`+`src/ui` |
| 渲染 | `src/render` 553 行 | `src/render2` 3,365 行 | render2；旧 render 仍被旧 Viewport 路径引用 |
| HTML | `src/html` 4,780 行（含 1,227 行测试文件） | `src/html2` 4,097 + `htmlcache` 1,714 | html2 复用 `html/` 的 CDP/Browser 源码 |
| MCP | `src/mcp` 6,506 行 | `src/mcp2` 13,288 行 | mcp2；旧 McpServer 仅旧 ctor |
| 音频 | `src/audio` 268 行 | `src/audio2` 1,783 行 | audio2；旧 AudioEngine 仅旧 ctor |

关键事实：

- `CMakeLists.txt:45` 明确把 `src/core/Project.cpp` 编进交付 target；`:175` 又链 `pvf_next_document`（`pvf_model` + `pvf_store`）。所以 `pvforge` 的符号表里同时有 `pvf::Project` 与 `pvf::model::Project`。
- `src/app` + `src/ui` + `src/mcp` 共 15 个头文件仍 `#include "core/Project.h"`；其中 11 个保留 `Project*` 旧构造函数（`MainWindow` + 10 个面板/视图）。交付启动路径只用 `EditorSession*` 构造函数，因此这 11 个 ctor 与它们拖着的实现都是**可达性为零的编译单元**。
- 旧 `Project.cpp` 反向依赖 `html/HtmlAsset.h`、`media/AudioDecoder.h`、`media/VideoDecoder.h`（`Project.cpp:3-6`）。这使「最简单」的工程数据层无法脱离 Chrome/FFmpeg 编译。
- `render2` 又依赖 `core/TextBlock/TextStyle/VectorShape`（`render2/Render.cpp:3-5`）。「新后端」事实上依赖「旧模型目录」，目录名已经不能表达层次。
- 同名双实现 8 对，合计 20,395 行；其中 `PropertiesPanel`（5,539 + 3,853）、`TimelinePanel`/`TimelineWidget`（4,660 + 3,293）、`Viewport`/`ViewportWidget`（4,424 + 1,906）是三对最大的平行实现。
- 重复 `Easing` 是最纯粹的浪费：`core/Easing.cpp` 327 行、`model/Easing.cpp` 328 行，diff 仅 380 行且主要是命名差异；30 个预设两份维护。

### 3.2 交付构建 vs 参考构建 vs 测试构建

`CMakeLists.txt` 实际结构：

- 2 个产品可执行文件：`pvforge-mcp`（31 行）、`pvforge`（43 行）。
- 24 个 `add_subdirectory`（`:148-171`），其中 `src/model/store/render2/html2/mcp2/...` 是新后端，`src/next/*` 8 个是参考壳/服务。
- 27 个静态库；`tr ' ' '\n' < build-release/CMakeFiles/pvforge.dir/link.txt` 显示交付链接 23 个，**4 个库只构建不链接**（`pvf_next_app`、`pvf_next_properties`、`pvf_next_timeline`、`pvf_next_viewport`）。
- 31 个测试可执行文件、330 个 CTest；顶层 `include(CTest)` 默认 `BUILD_TESTING=ON`。
- `install(TARGETS pvforge pvforge-mcp)` 只安装 2 个二进制，但默认 `cmake --build` 会构建全部 27 库 + 33 可执行文件。

不可达旧实现（逐文件核对，合计 12,349 行）：

```
core/Project.cpp 2961, mcp/McpCommandService.cpp 2389,
mcp/McpServer.cpp 1773, core/CheckpointManager.cpp 1425,
html/HtmlRenderer.cpp 1164, core/Commands.cpp 941,
render/SceneRenderer.cpp 386, media/VideoPlayer.cpp 377,
core/Easing.cpp 327, media/VideoProxy.cpp 223,
audio/AudioEngine.cpp 215, render/CameraRenderer.cpp 99, core/History.cpp 69
```

注意：这里说的是「交付启动路径不可达」，不是「测试不可达」。旧 ctor 被 `--self-test`、`--session-test` 等 101 个入口大量使用，所以不能直接删，必须先迁测试（D03/D28）。

参考壳（默认编译、交付不链接）17,219 行的构成：

- `next/app`：`NextWindow.cpp` 4,014、`CameraMonitorWidget`、`WorkbenchPanel`、`ExportDialog`（参考壳窗口）。
- `next/properties`：`PropertiesPanel.cpp` 3,853 及其对话框（除 3 个被交付直接编译的文件）。
- `next/timeline`：`TimelineWidget.cpp` 3,293。
- `next/viewport`：`ViewportWidget.cpp` 1,906。
- `next/assets`：`AssetPanel`。

**结论**：当前没有「交付构建」与「参考构建」的 CMake 边界，只有一个「全都编」的构建。这是「架构混乱」最直接的体感来源。

### 3.3 main.cpp：测试不是「放在 main」，而是「main 是测试运行器」

- 总行数 26,953；`int main` 从 26,621 开始。
- 第一个测试函数 `selfTest()` 在 145 行；测试函数区间 145-26,620，共 26,476 行（98.2%）。
- 测试分发 26,663-26,894，共 232 行，104 个 `app.arguments().contains(...)` 检查；`--*-test` 唯一开关 101 个（另有 `--timeline-edit-e2e`、`--cache-*`、`--special-demo` 等）。
- 生产逻辑：1-144（include + 约 60 个 include，含测试头）+ 26,621-26,662（44 行 App 初始化）+ 26,895-26,953（59 行 `--open`/欢迎窗）。约 **245 行 = 0.9%**。
- 测试代码还反向进入交付：`src/mcp/StdioBridgeTest.cpp` 962 行、`src/html/HtmlCaptureTest.cpp` 859 行、`src/html/HtmlLayerTest.cpp` 368 行被直接编进 `pvforge` target（`CMakeLists.txt:68,75-76`）。
- Release 验证：`cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF` 后，`strings -e l dist/pvforge` 仍能找到 101 个 `--*-test` 字符串；`nm` 有 28 个匹配测试函数符号。

**风险**：这不是「文件大」的审美问题，而是发布物、编译时间、review 成本、测试隔离四件事同时被绑死。拆成 `pvforge-tests`（或 `libpvforge_app` + `pvforge` + `pvforge-tests`）是后续 CI 与 target 拆分的前置条件。

### 3.4 工程格式与序列化

两套格式的键对照：

| 键 | 旧 `core::Project` | 新 `model::Project` |
|---|---|---|
| `format` | `"pvforge"` | `"pvforge-project"` |
| `version` | `4`，接受 1..4 | `1`，严格等于 |
| 数据 | 单文件含 entities/scenes/templates | `project.json` 只含 settings+markers，实体拆文件 |
| 迁移 | `Project.cpp:2520` 版本范围检查；`:2929` v1-v3 包默认场景 | 无迁移分支 |

两套校验也不等价：`core::validateProjectDocument`（`core/Project.cpp:248`）校验旧 scenes/templates/实体 id；`model::validateProject`（`model/Project.cpp:201`）校验 UUID 索引、父范围、父环、求解目标环、marker。字段层面 `name/width/height/fps/background` 两边各写一遍；旧模型多 `author/description/activeCamera/monitorView`，新模型多 `markers`。

具体风险点：

1. **升版即锁死**：`ProjectStore.cpp:264` `version != kProjectFormatVersion` 直接拒绝。新模型一旦加字段并升到 2，所有 v1 工程都打不开，必须先写迁移；当前没有迁移框架。
2. **同名键不同义**：两套都写 `format`/`version`，但常量不同。任何脚本/工具按「读 version 判断新旧」都会误判。
3. **缺失字段静默默认**：`model/Entity.cpp:166-181` 对 `start/end/row/hidden/...` 全部给默认值，`end` 缺失时默认 `start+1`。文件损坏不会报错，会变成一个 1 帧实体，保存后原值永久丢失。
4. **未知字段丢弃**：`toJson(Entity)` 只写已知字段。将来新版写入的扩展字段，被旧版打开再保存会静默消失。没有 `_extra` passthrough。
5. **多文件事务非原子**：`commitChanges` 逐实体 `saveEntity`，中途失败会留下部分写盘（`ProjectDocument.cpp:232-254`）。git 只在 debounce 后提交，崩溃窗口内依赖 dirty-text 恢复。
6. **提交消息参数是死的**：`ProjectDocument.cpp:182/197/215/237/261` 全部 `Q_UNUSED(message)`，最终统一写 `"pvf: human edit"`。UI 传的操作描述没有任何效果。

### 3.5 并发与生命周期

现状不是「到处乱用线程」，而是「每个子系统各自实现一套正确但昂贵的线程契约」：

| 位置 | 线程 | 所有权/关闭 | 评价 |
|---|---|---|---|
| `html2/HtmlFrameService` | 3 个 `QThread`（frontend/cache/disk）+ final 渲染器 | 手工 `new`，`stop()` 逐一 `requestStop()+wait()+delete` | 有线程亲和检查（`:710`、`:971`）；跨线程 final 渲染器选择「泄漏而不是错删」，诚实但难看 |
| `video2/VideoExtractionQueue` | 1 个 `QThread` | 成员裸指针，析构 `requestStop()+wait()+delete` | 契约集中，风险可控 |
| `workbench/PrewarmService` | 每任务 1 个 `QThread` | 裸 `new Task`，generation 防迟到回调，`delete`/`deleteLater` | 写法正确，但整块是死代码（D31） |
| `next/app/WorkbenchServices` | `QThreadPool`（1..4 writer） | `QThreadPool` 有 parent；析构 `discardPendingStores()+waitForDone()` | 编码结果回调用 `QueuedConnection` 丢弃迟到的对象 |
| 导出 | `QtConcurrent::run` | 捕获 `shared_ptr` 快照、原子取消标志 | 但直接调用 UI 线程的 `MediaServices`（D21） |

计数：5 个 `QThread` 子类、1 个 `QThreadPool`、36 处 `deleteLater`、25 处显式 `Qt::QueuedConnection`、生产代码 3 处 `processEvents`、生产代码 2 处 `msleep`（`waitForPendingStores` 轮询 1 处、`PrewarmService` 死代码 1 处）。

**主要债务**：

- 没有统一 `Worker` 基类/所有权类型，6 套「requestStop + wait + delete」靠复制粘贴维持正确。
- `waitForPendingStores` 用 `processEvents + msleep(1)` 轮询（`WorkbenchServices.cpp:392`）。虽然注释说明只给测试/profile 用，但 `main.cpp` 有 4 个性能测试调用它，任何人复制这段就会把轮询带进生产。
- `EditorSession.cpp:164` 在 MCP 批处理中 `processEvents()`，目的是让急停按钮可点；但这允许任意 UI 槽在事务中途重入。`HttpServer.cpp:227` 的注释承认了这点，却没有结构化事务边界。
- MainWindow 双模式：327 处 `m_project` 引用只有 4 处守卫。当前靠「旧菜单只在旧 ctor 创建」避免崩溃，类型系统没有提供任何保证。

### 3.6 错误处理与 i18n

- 中文硬编码生产代码 4,705 处（含测试 5,204 处），覆盖 93 个生产文件（含测试 114 个）。Top：`main.cpp` 468、`ui/PropertiesPanel.cpp` 374、`app/MainWindow.cpp` 325、`next/app/NextWindow.cpp` 287、`next/properties/PropertiesPanel.cpp` 284。
- `Lang::t` 仅 77 次：`MainWindow.cpp` 62、`ui/WorkbenchPanel.cpp` 12、`main.cpp` 2、`AssetLibraryPanel.cpp` 1。语言包 `zh_CN`/`en_US` 各 49 key，只覆盖菜单、工具栏、少量标题。
- 设置页有「English」选项（`SettingsDialog.cpp:900-906`），切换后大部分界面仍是中文——这是「假 i18n」，比没有切换更伤信任。
- 后端错误：`store`/`model`/`export2`/`mcp2`/`html2`/`video2` 共 762 条生产 `QStringLiteral("中文…")`；`mcp2` 有结构化 `code`，其他层没有。
- 类型层面：209 个头文件函数用 `QString* error` 出参，没有错误枚举/分类。
- 最糟的模式是「错误串当 API」：`ExportDialog.cpp:45-57` 直接比较 `"output_exists"` / `"encoder_unavailable"` / `"invalid_crf"` / `"cancelled"`；`Export.cpp` 里校验错误用英文 code，运行期错误用中文串（如 `"素材缺失"`、`"音频解码失败"`）。结果：英文用户看到中文，中文用户看到英文，UI 分支绑死在文案上。

### 3.7 测试 / CI / 发布

- 330 个 CTest：`mcp2` 55、`timeline` 54、`workbench` 38、`store` 32、`render2` 32、`video2` 25、`export2` 22、`assets` 17、`html2` 16、`htmlcache` 13、`audio2` 9、`next` 8，其余单测。
- 最近一次完整运行：`build-coord-tests`，2026-11-12 22:22，330 项中 3 失败。总耗时约 74 秒（`CTestCostData` 求和），**对 CI 完全可接受**。
- 旧 `build` 目录仍是 327 项（缺 3 个异步 writer 测试），说明构建目录本身也在漂移。
- 101 个 `--*-test` 与 330 个 CTest 覆盖高度重叠：例如 MCP 既有 `mcp2` 55 项，又有 `--mcp-test`/`--mcp-http-test`/`--mcp-integration-test` 等；时间轴既有 `timeline.*` 54 项，又有 `--timeline-*` 十余项。
- 无 CI：没有 GitHub Actions/GitLab CI；无 PR 检查；无夜间构建；无 sanitizer job。
- 无 `CMakePresets.json`：Debug/Release/`BUILD_TESTING=OFF` 只能靠 README 里的长命令口口相传。Release 配置只存在于 `dist/RUN.txt`。
- 发布是动态链接：15 个直接 `NEEDED`（Qt Sql/Multimedia/Network/Widgets/Gui/Core + FFmpeg 5 库 + libc/libstdc++），传递依赖 177 行。README 建议 Ubuntu 26.04 / Qt 6.10 / FFmpeg 8；在 Ubuntu 24.04（Qt 6.4.2）上连 CMake 配置都过不了（据 #78，本机为 26.04/Qt 6.10.2，未复现 24.04）。

### 3.8 缓存 / 性能复杂度：哪些必要，哪些重复

| 机制 | 规模 | 判定 | 理由 |
|---|---|---|---|
| 工作台整帧缓存 | `workbench` 生产 2,244 行（Cache 751 + MetaBuilder 453 + Qoi 315 + 服务） | **必要** | 2:30 PV 从头播要避免逐层求值；按需写入、元信息签名失效是正确设计 |
| QOI + WebP 双编码 | `Qoi.cpp` 315 行；`Cache.cpp` 32-133 | **必要** | 纯矢量帧无插件、无损、同步 2ms；栅格帧 WebP 80 走后台池。两条路径有明确性能理由 |
| HTML 三浏览器 | `html2/HtmlFrameService` 1,189 行 + `htmlcache` 1,277 行 | **必要但偏重** | 前端低清/缓存低清/导出高清的分工合理；代价是 3 套 Chrome 生命周期和 2 个磁盘缓存 |
| 双 SQLite/LRU/WebP 索引 | `workbench/Cache.cpp` + `htmlcache/DiskCache.cpp` | **重复** | 两套 schema、两套 LRU、两套损坏恢复；可抽公共 `frame cache` 后端（D30） |
| 求解预计算 | `EditorSession.cpp:304-345`；`anim` 共享结果表 | **必要** | 每段只解一次，视口/时间轴/属性/MCP 复用；播放时锁缓存避免卡帧 |
| `PrewarmService` | 407 行 | **死代码** | 生产无引用；当前是按需缓存，文档也说「不预热」 |
| 视频抽图/代理 | `video2` 生产 1,801 行 | **必要** | 全量 WebP 序列 + LRU 回退原片，是播放流畅的关键；但和 HTML 缓存不共享基础设施 |

结论：缓存层不是「过度设计」，而是「正确设计做了两遍」。先保留行为，再合并基础设施；不要为了「简化」砍掉 QOI/三浏览器/预计算。

### 3.9 文档债与 TODO

- `docs/` 共 31 篇、13,067 行（`docs/AI` 29 篇 + 2 篇用户文档），AI 文档全部有横幅，方向正确。
- 但 `STATUS.md` 是 2026-11-11 的快照：最新提交写 `491c270`，实际 HEAD 已在其后 55 个提交；CTest 写 268，实际 330；二进制写 9.5MB，实际 11MB；已知失败仍写 3 个 `--timeline-*`，实际当前失败是 `viewport.tests`/`next.media_services`/`next.camera_monitor`。
- `UI_BACKEND_INTEGRATION.md` 说旧 `core/Project.*`「只是从新目标链接列表移除」，但 `CMakeLists.txt:45` 仍在编、`:175` 仍链新模型；文档与实现相差一次方向修正。
- `INTEGRATION_PLAN.md` 仍写「进行中：Watchdog、Store 崩溃恢复」，两者早已完成；`UI_COMPARISON_V020.md` 基准是 `HEAD 58b16fa`。
- `dist/RUN.txt` 版本 0.2.0，README/CMake 0.3.0。
- TODO/FIXME/HACK 只有 3 处真实条目（`WaveformView.cpp:343`、`AssetLibraryPanel.cpp:2221`、`ViewportWidget.h:49`）——这是**优点**，说明作者不靠 TODO 记账。真正的债在「已取消/已修正/进行中」这类叙述里，而不是 TODO 注释里。

---

## 4. Top 10 修复顺序

排序原则：先降「持续制造新债」的速率，再降单点风险；先做能解锁后续工作的，不按文件大小排。

| 排名 | 动作 | 为什么先做 | 依赖 | 完成标志 |
|---|---|---|---|---|
| 1 | 拆 `main.cpp`：`libpvforge` + `pvforge` + `pvforge-tests`（#76） | 一次解决发布物含测试、单文件 review、编译反馈慢；是 CI 与 target 拆分的前置 | 无 | Release 二进制扫不到 101 个测试开关；单文件 < 1,500 行 |
| 2 | 建 CI + `CMakePresets.json`（#78） | 330 个测试 74 秒可跑完，却没有守门人；先让「红/绿」可信 | 1（测试 target 可单独构建） | PR 必跑 build+ctest；3 个失败先 `xfail` 或修复 |
| 3 | CMake 三分：delivery / reference / tests（#75 #77） | 停止默认编译 17.2k 行参考壳与 12.3k 行不可达旧实现 | 1、2 | 默认目标只含交付依赖；参考壳需显式 `-DPVF_BUILD_REFERENCE=ON` |
| 4 | 统一工程格式与迁移框架（#75） | 双 version 是数据风险；升版前必须先把迁移做出来 | 3 | 只有一套 format 常量；v1→v2 迁移有测试 |
| 5 | 让 330 项全绿：修 3 个失败 + 退役旧测试（#72-74） | 失败测试会让真回归不可见；退役测试与旧栈清理互相解锁 | 1 | `ctest` 0 失败；失败清单为空 |
| 6 | 错误模型 + i18n 基础（#81） | 4,705 处中文不是一次性翻译能解决；先定 `ErrorCode` 与 key 规范，禁止新硬编码 | 3 | 后端错误 100% 带 code；新代码 CI 禁止新增中文字面量 |
| 7 | 提交契约：删掉死 `message` 或真正使用；明确 undo 粒度（新 issue） | 当前 Git 历史全是 `pvf: human edit`；undo 可能跨手势 | 无 | 历史能看出操作类型；Ctrl+Z 一步一手势或有明确文档 |
| 8 | 并发所有权收口 + 修导出线程（#80、D21） | 5 套手工 QThread 契约 + UI 服务跨线程读文档，是最可能出「偶发崩溃」的地方 | 3 | 无裸 `new QThread`；导出期间 MCP 写被阻止或用快照 |
| 9 | 用信号/条件变量替换 `waitForPendingStores` 轮询（#79） | 轮询是测试不稳定与重入的温床；改动小、收益直接 | 1 | 测试不再 `processEvents+msleep` |
| 10 | 合并两套缓存基础设施 / 删除 PrewarmService（D30、D31） | 正确设计做了两遍；先量命中率再合并，避免为简化砍功能 | 3、8 | 单一 frame-cache 后端；无死代码 |

**建议先修的 3 条：1 → 2 → 3。** 理由：它们互相依赖且共同决定「后面所有工作是否可验证」。先拆 main.cpp，CI 才有意义；先有 CI，target 拆分才不会把功能拆坏；先拆 target，双模型/双后端的清理才有安全网。

---

## 5. 分阶段计划

### 第 1 周：止血与可验证

| 任务 | 交付物 | 验收 |
|---|---|---|
| 拆 `main.cpp`（D03/D04/D28） | `pvforge-tests` target；`main.cpp` 只留启动与参数解析 | Release 无测试开关；`--*-test` 从测试 target 提供 |
| CI + Presets（D11/D12/D14） | `.github/workflows/ci.yml`、`CMakePresets.json`（debug/release/asan） | PR 必跑；330 项 ctest 约 75 秒内完成 |
| 测试基线（D27） | 3 个失败修复或显式 `xfail`，记录原因 | `ctest` 0 unexpected failure |
| 文档数字校正（D32/D33） | STATUS 顶部改为「由脚本生成」；RUN.txt 版本修正 | 数字与 `ctest -N`/`ls -lh` 一致 |

### 第 2 周：把双栈从构建图里摘出去

| 任务 | 交付物 | 验收 |
|---|---|---|
| CMake 三分（D05/D06/D07） | `PVF_BUILD_REFERENCE`、`PVF_BUILD_TESTS`；交付 target 只链 23 个必要库 | 默认 build 不编 `pvf_next_*` 面板库 |
| 旧 ctor 迁移/隔离（D02） | 旧 `MainWindow(Project*)`、11 个旧构造函数移入测试专用源文件或 `#ifdef PVF_LEGACY_TESTS` | 交付 objs 不再含 `core/Project.cpp`、`McpServer.cpp`、`HtmlRenderer.cpp` |
| 格式单一化 A（D16） | 冻结 `kProjectFormatVersion` 为 1；新增迁移入口与 round-trip 测试 | 打开 v1 工程 + 升版拒绝信息可读 |
| 提交契约（D18） | 决策：保留 debounce 合并 → 删除 `message` 参数；或按手势 flush | 代码与文档一致，历史可读 |

### 第 1 个月：结构性清理

| 任务 | 交付物 | 验收 |
|---|---|---|
| 单一文档层（D01/D09/D10） | `model::Project` 为唯一数据源；旧 `core::Entity` 只保留 DTO 并迁到 `app/`；删除 `core/Project.cpp`、`core/Easing.cpp` | `pvf::Project` 符号从交付消失；两个 Easing 合一 |
| 错误/i18n（D24/D25/D26） | `ErrorCode` 枚举 + `Lang` key 规范；后端错误全部带 code；UI 不再比较错误串 | 新增中文字面量在 CI 被拦截；English 切换覆盖全部面板/错误 |
| 并发（D20/D21/D22） | 统一 `Worker` 所有权；导出线程只读快照或用队列；测试等待改信号 | ASAN/TSAN job 通过；无 `processEvents+msleep` 轮询 |
| 缓存（D30/D31） | 抽公共 frame-cache（SQLite+LRU 一套）；删除或接入 `PrewarmService` | 两处缓存共用一套索引/淘汰测试 |
| 旧栈收尾（D36） | 给每个保留旧文件写「删除条件/负责人/日期」；无退出条件的直接删 | `git ls-files src` 不再有「仅测试可达」的交付源文件 |

---

## 6. 已有优点（不要一边倒）

这些设计是合理的，清理时应保留而不是「重写」：

1. **MCP 安全边界扎实**：只监听 `127.0.0.1`（`mcp2/HttpServer.cpp:522`），Bearer token 持久化，配置/端点文件强制 `0600`（`:83-106`、`:144`），重复 `Host/Content-Length/Authorization` 按请求走私拒绝（`:366`），显式拒绝 `Transfer-Encoding`/chunked（`:987-994`）。这是审计里少见的、做得比文档更严的部分。
2. **Chromium 隔离到位**：临时 `--user-data-dir`（`html/HtmlBrowser.cpp:190`）、死代理 `--proxy-server=http://127.0.0.1:9` + `--proxy-bypass-list=<-loopback>`（`:231-234`）、清理 `http_proxy/https_proxy/all_proxy/no_proxy` 环境（`:264`）、优先 `--remote-debugging-pipe`（`:242`）。恶意 HTML 很难回连外网。
3. **CheckpointManager 的 git 隔离**：独立 `--git-dir`、`GIT_DIR`/`GIT_HOME` 环境清洗（`core/CheckpointManager.cpp:510-540`）、固定 `checkpoint@pvforge.invalid` 身份、不碰用户仓库。虽然当前只在旧 MCP 路径使用（D02），实现本身是正确范本。
4. **Store 的写盘与恢复**：全程 `QSaveFile` 原子写（`ProjectStore.cpp:173-222`）；只管理 `project.json`/`entities/*.json`/`.gitignore` 文本；undo 是内存快照、不走 `git revert`；`restoreCommit` 只前进不 `reset --hard`；dirty-text 扫描给恢复/丢弃/提交三种选择。这是整个仓库最扎实的模块。
5. **工作台缓存是「按需」而非「预热」**：`WorkbenchServices` 只在视口真正绘制时 `lookup/store`，元信息按帧签名，纯矢量帧 QOI 同步写、栅格帧 WebP 后台写、播放期有界队列丢弃旧帧。性能与可预测性兼顾。
6. **求解预计算共享**：`EditorSession::scheduleSolverPrecompute()` 每次编辑只解一遍，结果进共享表；播放时 `solverResultsLocked` 禁止在绘制路径解算，避免掉帧。视口、时间轴、属性卡、MCP 预览读同一份结果。
7. **线程亲和性有显式检查**：`HtmlFrameService` 对 final 渲染器记录 `m_finalThreadId`，跨线程调用直接报错而不是错删；各 worker 析构都 `requestStop()+wait()`。不是「无脑 QThread」。
8. **测试覆盖密度高、命名可读**：330 个 CTest 大量按行为命名（`trimParentDeletesChild`、`restoreCommitRollsBackOnCommitFailure`），模块可独立 `cmake -S src/store` 构建。拆 target 后这是一笔现成资产，不需要重写测试。

---

## 7. 建议新开 issue

> 以下只是建议，未创建。

| 建议 | 标题 | 依据 |
|---|---|---|
| N1 | 双模式 `MainWindow` 的 `m_project`/`m_session` 不变量应收紧 | 327 处引用 vs 4 处守卫；旧 ctor 交付不实例化（D19） |
| N2 | 导出线程不得直接调用 UI 线程 `MediaServices` | `MainWindow.cpp:3625` + STATUS.md:168 自认导出期未锁 MCP 写（D21） |
| N3 | 工程格式迁移框架与「升版即拒绝」策略 | `ProjectStore.cpp:264` 严格相等；`model/Project.cpp:344` 无迁移（D16） |
| N4 | 解析器应对缺失/未知字段报错或透传，而不是静默默认 | `model/Entity.cpp:166-181`、`toJson` 丢未知字段（D17） |
| N5 | 错误码取代错误字符串作为 UI 契约 | `ExportDialog.cpp:45-57`、`Export.cpp` 中英混用（D26） |
| N6 | `commitEntity(message)` 参数失效与 undo 粒度 | `ProjectDocument.cpp:19,182`；debounce 1s 合并（D18） |
| N7 | 合并 workbench 与 htmlcache 的 SQLite/LRU 基础设施 | `Cache.cpp` 751 行 + `DiskCache.cpp`（D30） |
| N8 | 删除或接入 `PrewarmService`（407 行死代码） | 生产无引用（D31） |
| N9 | 建立「文档数字自动生成」 | STATUS 的 268/9.5MB/491c270 全部过时（D32） |

---

## 附录 A：度量命令

```bash
# 代码量
git ls-files 'src/*' | xargs wc -l | tail -1
git ls-files 'src/*' | grep -viE 'test' | xargs wc -l | tail -1
git ls-files 'src' | grep -iE 'test' | xargs wc -l | tail -1
wc -l src/main.cpp

# main.cpp 结构
grep -nE '^(static )?(int|bool|void) [a-zA-Z0-9_]+\(' src/main.cpp | wc -l
grep -oE '"--[a-z0-9-]+-test"' src/main.cpp | sort -u | wc -l
grep -c 'app.arguments().contains' src/main.cpp

# 构建图
grep -rn 'add_executable\|qt_add_executable' --include=CMakeLists.txt . | wc -l
grep -rn 'add_library' --include=CMakeLists.txt . | wc -l
grep -rn 'add_test' --include=CMakeLists.txt . | wc -l
grep -rn 'add_subdirectory' CMakeLists.txt | wc -l
ninja -C build -t commands pvforge | tail -1 | tr ' ' '\n' | grep '\.a$' | sort -u

# 测试
ctest --test-dir build-coord-tests -N | tail -2
cat build-coord-tests/Testing/Temporary/LastTestsFailed.log
awk '{s+=$3} END {print s}' build-coord-tests/Testing/Temporary/CTestCostData.txt

# 发布物
readelf -d dist/pvforge | grep -c NEEDED
ldd dist/pvforge | wc -l
strings -e l dist/pvforge | grep -cE '^--[a-z-]+-test$'
nm -C dist/pvforge | grep -cE ' [Tt] .*(selfTest|sessionTest|timelineTest|exportTest)'

# i18n / 错误
grep -roP '(QStringLiteral|QString::fromUtf8|tr)\("[^"]*\p{Han}[^"]*"' src --include=*.cpp --include=*.h | wc -l
grep -rn 'Lang::t' src --include=*.cpp --include=*.h | wc -l
python3 -c "import json;print(len(json.load(open('assets/lang/en_US.json'))))"
grep -rn 'QString\* error' src --include=*.h | wc -l

# 重复实现
git ls-files src | awk -F/ '{print $NF}' | sort | uniq -d
wc -l src/core/Project.cpp src/model/Project.cpp src/core/Easing.cpp src/model/Easing.cpp

# 文档
git ls-files docs | xargs wc -l | tail -1
git log -1 --format='%h %ad %s' --date=short
git log --oneline 491c270..HEAD | wc -l
```

## 附录 B：原始数字

```
HEAD                              7215da8 (2026-11-12)
非测试源码                        149,117 行（.cpp/.h）；含 CMake 150,757 行
测试源码                          36,108 行 / 37 个 .cpp/.h（含测试 CMake 共 36,362 行 / 44 文件）
源码合计                          187,119 行 / 349 文件（318 个 .cpp/.h + 31 个 CMake）
main.cpp                          26,953 行（生产约 245 行，0.9%）
main.cpp 测试函数                 26,476 行（145-26620）
main.cpp 分发                     232 行（26663-26894）/ 101 个 --*-test / 104 个 contains
CMakeLists                        32 个 / 25 project() / 31 find_package(Qt6) / 50 CXX_STANDARD
静态库                            27 个（交付链接 23 个）
可执行文件                        33 个（产品 2 + 测试 31）
CTest                             330 个（build-coord-tests）；327 个（旧 build）
CTest 耗时                        约 74 秒（Last-run 求和）
已知失败                          3 个（viewport.tests / next.media_services / next.camera_monitor）
旧模型 core                       9,332 行 / 30 文件
新模型 model                      4,724 行 / 18 文件
store                             3,734 行 / 4 文件
旧 UI ui+app                      42,581 行 / 59 文件
参考壳 next                       41,531 行 / 73 文件（非测试 26,484，未链接 17,219）
旧后端 render+html+audio+mcp      12,107 行
新后端 render2+html2+htmlcache+audio2+mcp2  24,247 行
不可达旧实现                      12,349 行（13 个文件，见 §3.2）
同名双实现                        8 对 / 20,395 行
中文字面量                        4,705 处 / 93 个生产文件（含测试 5,204 / 114）
Lang::t                           77 次
语言包                            zh_CN 49 key / en_US 49 key
QString* error                    209 个签名
后端中文错误串                    762 条（store/model/export2/mcp2/html2/video2 生产代码）
MCP 工具                          46 个（kTools 数组实测）
真实 TODO/FIXME/HACK              3 处
legacy/旧壳/兼容 注释             183 处
文档                              31 篇 / 13,067 行
Release 直接/传递依赖             15 / 177
Release 大小                      10,933,616 字节（约 11MB，未 strip）
Release 测试开关                  101 个
```

## 附录 C：口径与未验证项

- 「交付启动路径不可达」按代码路径判断：`main()` 只构造 `MainWindow(EditorSession*)`，旧 `MainWindow(Project*)` 与 11 个旧构造函数不在该路径；不表示测试不可调用。
- 「未链接参考壳」按 `ninja -t commands pvforge` 的 `.a` 列表与 `src/next` 源文件集合求差，使用 `.cpp/.h` 行数，不含测试。
- `ctest` 数字取 `build-coord-tests`（最新、330 项）；`build` 目录为 327 项且缺 3 个异步 writer 测试，本身属于「构建目录漂移」证据。
- 未验证：Ubuntu 24.04 + Qt 6.4.2 的实际配置失败（据 #78）；8192×4320 HTML 与 4x/8K 导出的内存/耗时；VAAPI 真机编码；Windows/macOS 构建。
- 本报告未运行会写文件或启动 GUI 的测试，只做了只读统计与 `ctest -N`。
