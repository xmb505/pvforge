# TEST REVIEW — 2026-11-13

审查对象：`/home/xmb505/pvforge`，代码基线 `6eb80a0`（review 期间仓库从 `273d9a0` 前进到 `6eb80a0`，此后到 `43c53b8` 只有 README/docs 改动，测试相关代码未再变化）。
审查方式：只读代码 + 现场配置/运行取证，不修改代码、不提交。

结论先说：**这套测试的主要功能不是防止回归，是让报表好看。** 350 个 CTest 用例、100 个自检开关、25.6k 行 `src/tests`，但 CI 一个测试都不跑；覆盖交付 UI 的 25.6k 行测试不在 CTest 里；能跑的那部分里至少 140 个用例可以在缺工具时静默 skip；交付二进制上的“验收命令”是假的。

---

## P0：这几条不修，后面全是自我安慰

### 1. CI 根本不跑测试，只数行数

`.github/workflows/ci.yml` 全文只有 `code-size` 一个 job：

- `.github/workflows/ci.yml:6-9` 注释直接承认：“构建、测试、Release 仍由本地手动执行”
- `.github/workflows/ci.yml:17-26` 唯一 job 是 `bash tools/check-code-size.sh`

也就是说：**测试红不红，合并门禁完全不知道。** 上一版 CI 至少有构建矩阵，`git show ed013df~1:.github/workflows/ci.yml` 里还能看到 `build-test`、`ctest`、`关键自检`；现在这些 job 被整体删掉，而不是被修绿。

同时 `ci/README.md:102-111` 仍在写“CI 只跑 Linux x86_64 + offscreen”“运行环境是 GitHub hosted runner……全量构建 + 测试约 15–25 分钟”，文档描述的 CI 与仓库里的 CI 不是同一个东西。

### 2. `pvforge-tests` 从未注册进 CTest，25.6k 行测试是手动的

顶层 `CMakeLists.txt:199-230` 只创建了 `pvforge-tests` 可执行文件，**没有任何 `add_test()`**。模块测试（`src/*/tests`）全部通过 `add_test` 注册，唯独最大的 `src/tests` 没有。

现场取证：

```text
cmake -S . -B /tmp/opencode/pvf-review-config -G Ninja \
      -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DPVFORGE_BUILD_SELF_TESTS=ON
# 成功
ctest -N  ->  Total Tests: 350
grep pvforge-tests CTestTestfile.cmake src/*/CTestTestfile.cmake  ->  无匹配
```

数字对比：

| 项目 | 数量 |
|---|---|
| CTest 用例 | 350 |
| `registry.add(...)` 自检入口 | 100 |
| `src/tests/**` 行数 | 25,624 |
| 其中被 CTest 调度的入口 | 0 |

`src/tests/TestRegistry.cpp:31-50` 注册了 13 个测试翻译单元；`src/tests/TestRegistry.h:40-54` 是唯一的“测试清单”。**清单只存在于源码里，任何自动化都不会执行它。** README 也只能写“完整清单以源码为准”（`README.md:75`）。

更糟的是没有批量 runner：`Registry::run()` 只按参数顺序执行**第一个**命中的开关（`src/tests/TestRegistry.cpp:19-29`），`main` 拿到结果就 `return`（`src/main.cpp:63-68`）。所以 `pvforge-tests --self-test --timeline-test` 只会跑第一个，进程照样退出 0——多开关只会静默吞掉后面的测试。

### 3. 交付目录里的“验收命令”在交付二进制上根本不存在

`dist/RUN.txt:53-63` 写着“验收命令（本机已通过）”，列了 9 条：

```text
QT_QPA_PLATFORM=offscreen ./pvforge --self-test
QT_QPA_PLATFORM=offscreen ./pvforge --session-test
...
```

但 `dist/pvforge` 就是 `build-release/pvforge`（md5 一致），其 cache 是：

```text
BUILD_TESTING:BOOL=OFF
PVFORGE_BUILD_SELF_TESTS:BOOL=OFF
CMAKE_BUILD_TYPE:STRING=Release
```

`src/main.cpp:37-68` 的测试分发全部包在 `#ifdef PVFORGE_SELF_TESTS` 里；Release 的 `pvforge` 里没有 registry，`--self-test` 只是普通未知参数，会继续进入欢迎窗口。

现场运行：

```text
$ QT_QPA_PLATFORM=offscreen timeout 8 ./dist/pvforge --self-test
This plugin does not support propagateSizeHints()
exit=124        # 挂住，被 timeout 杀掉
```

同一文件 `dist/RUN.txt:49` 自己就写着 “Release，BUILD_TESTING=OFF”，第 53-63 行又列自检验收命令。**这不是笔误，是把两类构建的结论混在一张验收单上。**

### 4. 测试构建和交付构建是两套程序：测试强制打开 `PVFORGE_LEGACY_SHELL`

- `CMakeLists.txt:42-45`：只要 `PVFORGE_BUILD_SELF_TESTS=ON` 且 legacy 关着，就**强制** `PVFORGE_LEGACY_SHELL=ON`
- `CMakePresets.json:19-24`：debug preset 直接 `PVFORGE_LEGACY_SHELL=ON`
- `CMakePresets.json:31-36`：release preset `PVFORGE_LEGACY_SHELL=OFF`

于是所有自检链接的是 **带旧 `core::Project` 壳的 `pvf_app`**，而交付的 `pvforge` 链接的是**不带旧壳的 `pvf_app`**。`src/app/MainWindow.cpp` 里有 30+ 处 `#if PVFORGE_LEGACY_SHELL`，测试覆盖的是 ON 分支，交付跑的是 OFF 分支。`MainWindow(Project*)` 构造函数整段只在 legacy ON 时存在（`src/app/MainWindow.cpp:348` 起），而 UI 测试几乎都用它，例如：

- `src/tests/WorkbenchTests.cpp:25-28`（`auto* project = new pvf::Project;` + `MainWindow window(project)`）
- `src/tests/AssetTests.cpp`、`EditorTests.cpp`、`PropertiesTests.cpp` 同样大量使用 legacy 构造函数

按“包含 `LegacyTestSupport.h` 的翻译单元”统计，100 个自检入口里有 **88 个**直接依赖退役模型。也就是说：**测试把一个 Release 里不存在的壳当成主程序在测。** 任何只影响 Release 分支的回归，这套测试从结构上就看不见。

### 5. “skip”被当成“通过”：自定义测试框架把跳过打印成 PASS

`src/html2/tests/Html2Tests.cpp` 的 19 个 CTest 用例里，**17 个**在没有 Chromium 时走：

```cpp
if (!chromeAvailable())
{
    QTextStream(stdout) << "SKIP (QSKIP): no Chromium-based browser found\n";
    return;                      // 不是 QSKIP，就是普通 return
}
```

位置：`Html2Tests.cpp:422-427, 486-491, 563-568, 623-628, 673-678, 749-754, 856-861, 920-925, 988-993, 1068-1073, 1144-1196, 1226-1231, 1290-1295, 1368-1373, 1434-1439, 1491-1496, 1555-1560`。

`main()` 最后统一打印：

```cpp
out << (gFailures == 0 ? "PASS " : "FAIL ") << (gChecks - gFailures) << '/' << gChecks << " checks\n";
return gFailures == 0 ? 0 : 1;
```

`Html2Tests.cpp:1685-1687`。所以没有浏览器的机器上，17 个用例会输出 `PASS 0/0 checks` 并以 0 退出。CI 环境按 `ci/README.md:106` 自己承认“没有 Chrome/Chromium”，即 **17 个“通过”的用例实际一行断言都没执行**。而且消息里还写着 `SKIP (QSKIP)`，骗读者以为走了 Qt 的 skip 机制。

同类问题（直接返回 0 / 只跳过一段继续算成功）：

| 位置 | 门槛 | 后果 |
|---|---|---|
| `src/tests/RenderMediaTests.cpp:1415-1421` | 没有 `ffmpeg`/`ffprobe` | `--export-test` 直接 `return 0`，验收单上算“通过” |
| `src/tests/RenderMediaTests.cpp:153-156` | 没有 `ffmpeg` CLI | video proxy 整段不测，仍返回 0 |
| `src/tests/SessionTests.cpp:1449-1456` | 没有 git | `--git-test` 跳过写/恢复检查，仍返回 `failures == 0 ? 0 : 1` |
| `src/html/HtmlCaptureTest.cpp:416-420` | 没有 Chrome | `return 0` |
| `src/html/HtmlLayerTest.cpp:164-169` | 没有 Chrome | `return 0` |
| `src/tests/ProfileFixtures.cpp:1214-1225` | 本机 `/home/xmb505/视频/...` 不存在 | benchmark 直接 `return 0` |
| `src/mcp/StdioBridgeTest.cpp:954-956` | 没定义 `PVFORGE_MCP_BINARY` | 跳过 CLI 检查，继续按成功返回 |

注意最后一条：`PVFORGE_MCP_BINARY` 只在 `pvf_app` 的 PRIVATE 定义里（`CMakeLists.txt:160-164`），如果哪天有人把 `StdioBridgeTest.cpp` 挪到另一个 target，这段会静默变空。

### 6. `git-lfs` 假门槛：生产代码只用 `git`，测试却按 `git-lfs` 整批跳过

`src/store/ProjectStore.cpp` 里只有 `runProcess(QStringLiteral("git"), ...)`（`ProjectStore.cpp:81, 100, 877`），全仓库 `src/store`、`src/mcp2` 生产代码**没有任何 lfs 调用**。但：

- `src/mcp2/tests/mcp2_tests.cpp:31-43` 先检查硬编码路径 `/tmp/opencode/gitlfs/root/usr/bin`，再要求 `git-lfs version` 成功；不满足就在 `initTestCase` 里 `QSKIP`（`:223-232`）
- `src/mcp2/tests/http_server_tests.cpp:36-50, 242-251` 同样逻辑
- `src/next/app/tests/MediaServicesTests.cpp:28-36, 138-141` 用 `git-lfs version` 判断 “store creation is unavailable”

影响面：

| 测试 | 用例数 | 没有 `git-lfs` 时 |
|---|---|---|
| `src/mcp2/tests/mcp2_tests.cpp` | 46 | 46 个全 skip |
| `src/mcp2/tests/http_server_tests.cpp` | 9 | 9 个全 skip |
| `src/next/app/tests/MediaServicesTests.cpp` | 9 | 9 个全 skip |

55 + 9 = **64 个 CTest 用例因为一个生产代码不依赖的工具整批消失**。更荒唐的是 `gitLfsAvailable()` 检查的是 `git lfs version`（子命令），`MediaServicesTests` 检查的是 `git-lfs` 二进制，两套写法互不一致；`/tmp/opencode/gitlfs/root/usr/bin` 是某台开发机的手工安装目录，换机器必然不存在。

### 7. 额外的 skip 入口（数量级）

- `src/store/tests/store_tests.cpp:99-105`：没有 git 时 39 个用例逐个 `QSKIP`
- `src/video2/tests/*.cpp`：没有 ffmpeg 时 20 个用例在 `initTestCase` 里 `QSKIP`
- `src/assets/tests/TestAssets.cpp:392, 685, 754, 772`：ffmpeg/zip/symlink 不可用时 `QSKIP`
- `src/mcp2/tests/http_server_tests.cpp:571-572`：默认端口 47821 被占用就 `QSKIP("默认端口 … 被占用")`。端口冲突是环境问题，不是“这个功能不用测”
- `src/next/timeline/tests/timeline_tests.cpp` 有 1 处、`src/tests` 里多处

粗算：**350 个 CTest 用例中，最多 140+ 个在缺 ffmpeg/git/git-lfs/Chrome 的机器上会变成 skipped 或伪 PASS。** CI 没有统计 skip 数，因此“全绿”可以等于“几乎没跑”。

---

## P1：不是立即致命，但持续制造假绿

### 8. “测试”里混进了 benchmark、demo 和截图生成器

`TestRegistry` 的 100 个入口里不全是测试：

- `src/tests/ProfileFixtures.cpp:1211` 起是 `--cache-miss-profile`，依赖开发者家目录的 `/home/xmb505/视频/PVForge/未命名项目1.pvproj`
- `src/tests/ProfileFixtures.cpp` 的 `--cache-encoder-bench` 是纯测量，不设阈值，不判回归
- `src/tests/RenderMediaTests.cpp:1121` 的 `renderDemo` 注册成 `--demo-shot`（`:1815-1816`），只负责导入图片、截屏
- `src/tests/RenderMediaTests.cpp:944` 的 `specialDemo` 注册成 `--special-demo`

`tools/doc-metrics.sh:111-114, 158-159` 把 `--*-test` 开关数当成“测试指标”写进 STATUS。**加一个空开关就能把这个数字做大**，这正好奖励了上面这些非测试入口。

### 9. 截图不是断言

`src/tests` 里有 **81 处** `grab().save(...)` / `grab().toImage().save(...)`。绝大多数只检查文件能不能写，例如：

- `src/tests/AssetTests.cpp:197` 直接 `save("/tmp/opencode/pvforge-session-library.png")`，连返回值都不看
- `src/tests/AssetTests.cpp:304-310` 只检查 `save()` 成功
- `src/tests/EditorTests.cpp:667, 1006`
- `src/tests/PropertiesTests.cpp:590-592, 891-893, 1244-1246`
- `src/tests/WorkbenchTests.cpp:76, 95, 100, 176, 265, 368, 385, 396, 403`

没有人会去看这些 PNG，也没有基线图对比。它们让“测试”看起来在做视觉验证，实际只验证了 `/tmp` 可写。

### 10. 时间驱动 + 固定 sleep 的测试

- `src/tests/WorkbenchTests.cpp:132-136`：`QTimer::singleShot(600, ...)` 后断言 `frame >= 10`。机器负载一高就红，负载低就绿，没有任何时钟抽象
- `src/tests/WorkbenchTests.cpp:42, 92, 99`：400ms → 300ms → 250ms 三层嵌套定时器，任何一层提前 `return` 而没调用 `app.exit()`，进程就永久挂住；这些入口没有 CTest TIMEOUT 兜底，因为根本没注册
- `src/tests/AssetTests.cpp:197` 附近的布局检查依赖窗口已经完成一次布局，但没有 `QTRY_*`

### 11. `FakeDocument` 让时间轴测试只测控件，不测持久化

`src/next/timeline/tests/timeline_tests.cpp:33-95` 定义了 `FakeDocument`：

```cpp
bool commitEntity(...) override { ++commitCount; ...; return true; }   // 永远成功
bool commitEntities(...) override { ++commitCount; ...; return true; }
bool commitChanges(...) override { ++commitCount; ...; return true; }
```

54 个 CTest 时间轴用例全部基于它。`oneGestureCommitsOnceAndEscRestores`（`:1738-1780`）验证的是 `commitCount` 这个内部计数器，而不是“拖动后文件里真的写入了新 start”。真正的 `ProjectDocument` + git 写入路径在这个 suite 里没有覆盖；覆盖它的 `src/tests/TimelineEditTests.cpp` 又不在 CTest 里。

### 12. 断言实现细节，而不是用户可见行为

- `src/render2/Render.h:94-97` 导出了 `placeholderLabelForTest()`，实现是 `Render.cpp:1470-1487` 里一份**和真实绘制路径平行**的 switch；测试 `Render2Tests.cpp:526-527, 559-560, 594, 615, 640-641, 703-704` 只断言这个 helper 返回的字符串。真实占位卡片画在 `Render.cpp:261-306`，测试完全没碰。改坏 `drawPlaceholder` 的文案/样式，测试仍绿。
- `src/tests/SolverTests.cpp:1442-1456`：

  ```cpp
  if (countDelta == 1)
      qInfo() << "accepted as the single deferred precompute ...";
  if (!check(countDelta <= 1, ...)) return 1;
  ```

  先把 1 解释成“可接受”，再用 `<= 1` 放行。这个断言无法区分“实现正确”与“实现多跑了一次 solver”，实际是围绕当前实现写的橡皮筋。
- `src/tests/WorkbenchTests.cpp:161` 用 `setSelectedEntityForTest(entityId)` 模拟选中，而不是点击时间轴行；布局测试测的是“调用 setter 后 tab 出现”，不是用户操作路径。

### 13. 测试钩子直接进交付二进制

非测试源码里 `ForTest` 出现 **296 次**（54 次在 `src/app/MainWindow.h`，51 次在 `src/ui/TimelinePanel.h`，25 次在 `src/ui/Viewport.h`……），全部没有 `#ifdef` 保护。当前 `dist/pvforge` 里能直接 `nm` 到 **80 个** `ForTest` 符号：

```text
$ nm -C dist/pvforge | grep -c ForTest
80
```

连全局可变状态也在交付路径里：

- `src/app/EditorSession.cpp:29-30`：`g_mcpConfigPathForTest`、`g_mcpPortOverrideForTest`
- `src/app/EditorSession.cpp:121-128`：`setMcpConfigPathForTest()` / `setMcpPortOverrideForTest()` 是 public static，Release 也可调用
- `src/app/MainWindow.h:123-170` 一整片 `...ForTest()` 访问器

这不只是代码卫生问题：测试可以改全局 MCP 端口/配置路径，说明生产代码里有为测试开的旁路；而交付产物带着 296 个测试入口，行为面比测试覆盖的还大。

### 14. 没有一条测试覆盖交付入口 `--open` / `--shot`

`src/main.cpp:74-115` 解析并执行 `--open`、`--shot`；全仓库测试只构造 `MainWindow`/`EditorSession`，没有任何测试启动 `pvforge` 二进制验证这两个参数（`grep -R '"--shot"' src/tests src/*/tests` 无结果）。`--shot` 是导出截图/自动化的入口，属于交付 CLI，却完全没有回归保护。

唯一有 CLI 端到端测试的是 `pvforge-mcp`（`src/mcp/StdioBridgeTest.cpp:779-908`），而且只在 `--mcp-bridge-test` 里，不在 CTest。

### 15. 模块级覆盖缺口

- `src/perf/CMakeLists.txt` 没有任何测试 target；`FrameProfiler` / `SystemStats` 只被不在 CTest 的 `--perf-monitor-test` 间接使用（`src/tests/PerfTests.cpp:24-25, 378-379`）
- `src/app/MainWindow.cpp`（交付主窗口）没有任何 CTest 测试链接 `pvf_app`：`grep -R pvf_app --include=CMakeLists.txt src` 无结果；350 个 CTest 测试全部只链接模块库
- `src/tests/` 里 25.6k 行测试与 `src/*/tests` 的 30.8k 行测试是两套体系，前者没有任何自动入口

### 16. 文档与事实漂移

- `ci/README.md:108` 说“全量构建 + 测试约 15–25 分钟”，但当前 CI 没有构建/测试 job
- `ci/README.md:109-111` 仍把 `html2.asyncRequestLowRes` / `lowResPreemption` 的偶发失败说成“CI 用 `--repeat until-pass:2` 兜住”。当前 CI 连 ctest 都不跑；历史上 `ed013df~1` 的 workflow 确实有 `ctest --preset debug -j 4 --repeat until-pass:2`，并且把 `viewport.tests|next.media_services|next.camera_monitor` 整组排除后另跑一次 `continue-on-error`。**重试和 known-flaky 不是被修掉的，是跟着 CI 一起被删掉的。**
- `README.md:69-72` 用 `./build/pvforge-tests`、`ctest --test-dir build`，但 preset 的构建目录是 `build/debug`
- `README.md:82` 说 `--check` 由 CI 校验，CI 已无 doc-metrics job
- `dist/RUN.txt:1` 写 “PVForge 0.2.0”，`CMakeLists.txt:3` 是 0.3.0
- `ci/README.md` 与 `dist/RUN.txt` 的“323 个测试”也已过期：当前配置是 350（`ctest -N`），且不含 `pvforge-tests`

---

## 建议的修复顺序（只列，不改）

**P0（先恢复“测试失败会有人知道”）**

1. 加回一个阻塞式 CI job：configure → build → `ctest`。不允许 `--repeat until-pass`，不允许整组 `continue-on-error`；known-flaky 要么修，要么删。
2. 给 `pvforge-tests` 一个真正的 runner：要么把 100 个开关批量注册成 CTest 用例，要么加 `--all-tests` 顺序执行并汇总（出现一个失败就非 0）。同时拒绝“多个开关只跑第一个”的静默行为。
3. 用 Release 配置（`PVFORGE_LEGACY_SHELL=OFF`）至少编译并跑一组冒烟测试。现在测试二进制强制 legacy ON，交付分支没有任何测试。
4. 修正 `dist/RUN.txt`：删除 Release 二进制上不可能工作的 `--self-test` 验收命令，或明确说明自检只在 `pvforge-tests` 里。
5. 删掉 `git-lfs` 门槛和 `/tmp/opencode/gitlfs/root/usr/bin` 硬编码；按实际依赖检查 `git`。

**P1（把“假绿”变成“可度量”）**

6. 所有“打印 SKIP 后 return”改为真正的 `QSKIP` / CTest SKIP；CI 统计 skipped 数，超过阈值判红。
7. 把 296 个 `ForTest` 钩子收进 `#ifdef PVFORGE_SELF_TESTS`，或拆到 test-only 翻译单元；至少不要再让 `setMcpPortOverrideForTest` 这类全局开关出现在 Release。
8. 把 `placeholderLabelForTest` 这类“测试专用副本”改成对真实渲染结果的断言；`FakeDocument` 至少补一条真实 `ProjectDocument` 的持久化集成测试。
9. 去掉固定 sleep、三层定时器、无基线的截图断言；改用信号 spy / `QTRY_*` / 明确的超时失败。
10. 给交付入口 `--open`、`--shot` 加进程级测试；这是用户/脚本真正会调用的路径。

---

## 取证命令附录

```bash
git rev-parse --short HEAD                      # 6eb80a0
cmake -S . -B /tmp/opencode/pvf-review-config -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DPVFORGE_BUILD_SELF_TESTS=ON
ctest --test-dir /tmp/opencode/pvf-review-config -N   # Total Tests: 350
grep -R "pvforge-tests" /tmp/opencode/pvf-review-config/**/CTestTestfile.cmake   # 无

grep -rhoE 'registry\.add\(QStringLiteral\("--[a-z0-9-]+"\)' src --include=*.cpp | sort -u | wc -l   # 100
find src/tests -type f \( -name '*.cpp' -o -name '*.h' \) -print0 | xargs -0 cat | wc -l            # 25624

grep -c QSKIP src/html2/tests/Html2Tests.cpp    # 17
grep -c "grab().save\|grab().toImage().save" src/tests/*.cpp | awk -F: '{s+=$2} END{print s}'        # 81
grep -RIn "ForTest" --include=*.h --include=*.cpp src | grep -v /tests/ | wc -l                     # 296
nm -C dist/pvforge | grep -c ForTest                                                                 # 80

QT_QPA_PLATFORM=offscreen timeout 8 ./dist/pvforge --self-test; echo $?                              # 124
```

一句话总结：**这不是“测试不够多”，是“测试被配置成不会失败”。** 先让 CI 真的跑、让 skip 真的可见、让交付配置真的被测试，再谈覆盖率和用例数。
