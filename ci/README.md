# CI 与本地验证

CI 有三道 gate：代码体量 ratchet、Debug 构建 + 全量 `ctest`（含需要 Chrome 的 HTML 图层用例），
以及 Release 构建 + 交付检查（确认 `ForTest` 钩子没有漏进 Release）。本地命令见下。

## 依赖

```bash
ci/install-deps.sh              # apt：Ninja、ccache、FFmpeg 开发库、Qt 运行/构建依赖、Xvfb
ci/install-deps.sh --qt 6.7.3   # 额外用 aqtinstall 装 Qt 到 build/qt/6.7.3/linux_gcc_64
```

aqt 的 Linux 桌面架构名随版本变化：**6.6 及更早是 `gcc_64`，6.7 起是
`linux_gcc_64`**（`aqt list-qt linux desktop --arch <version>`）。脚本会自己查。

- **Ubuntu 24.04 自带的 Qt 是 6.4.2**（`qt6-base-dev`），低于 `CMakeLists.txt` 里的
  `find_package(Qt6 6.5 REQUIRED ...)`，直接 configure 会失败。本地必须用 aqt 装
  6.6.x/6.7.x（或发行版自带 ≥ 6.5 的 Qt，例如 26.04 的 6.10）。
- FFmpeg 版本差异（Ubuntu 24.04 的 6.1 vs 发布包 FFmpeg 8）需要本地另行验证。

## 构建与测试

```bash
cmake --preset debug                  # build/debug，Debug + BUILD_TESTING/SELF_TESTS=ON
cmake --build --preset debug -j 8
ctest --preset debug                  # 已带 QT_QPA_PLATFORM=offscreen 与 --output-on-failure

cmake --preset release                # build/release，Release，不构建测试
cmake --build --preset release -j 8   # 产物 build/release/{pvforge,pvforge-mcp}

cmake --preset tests                  # build-coord 风格：-O0 -g，全量测试
```

自检参数（离屏）：

```bash
mkdir -p /tmp/opencode               # solver/perf 自检会往这里写截图
QT_QPA_PLATFORM=offscreen ./build/debug/pvforge-tests --self-test
QT_QPA_PLATFORM=offscreen ./build/debug/pvforge-tests --solver-ui-test
QT_QPA_PLATFORM=offscreen ./build/debug/pvforge-tests --perf-monitor-test
QT_QPA_PLATFORM=offscreen ./build/debug/pvforge-tests --settings-test
```

## 构建开关

`CMakePresets.json` 是纯 JSON，不能写注释，所以记在这里。顶层 `CMakeLists.txt` 有两类目标：

- **delivery**：`pvforge`、`pvforge-mcp`，以及它们实际链接的模块（含
  `pvf_next_document`、`pvf_next_services`、`pvf_next_properties` 的
  `SolverPreviewWidget` 等、`pvf_next_timeline` 的 `MarkerDialog` 等）。
- **tests**：`pvf_*_tests` 可执行文件，只看 `BUILD_TESTING`。

对应选项：

- `PVFORGE_BUILD_SELF_TESTS`：把 `src/main.cpp` 里的 `--*-test` 自检开关编进/编出。
  Debug 默认 ON，Release 默认 OFF。

`debug` / `tests` preset 打开 self-tests；`release` 关闭。不用 preset 时默认是
Release 语义的裁剪构建。

## 文档指标

`docs/AI/STATUS.md` 顶部的指标区块不再手抄。原 `tools/doc-metrics.sh` 与 CI 的
`doc-metrics` job 已一并移除，需要数字时直接用 `tools/check-code-size.sh` 等现役工具或
命令行统计。

## 代码体量

`tools/check-code-size.sh` 防的是“又一个 5000 行文件”，不是把所有历史大文件一次砍平。

```bash
bash tools/check-code-size.sh                   # 检查，CI 用这个
bash tools/check-code-size.sh --update-baseline # 重新生成 baseline，只在人类明确要求时用
```

规则：

- **硬规则**：`src/main.cpp` ≤ 300 行。入口只做装配，逻辑放 `src/app` 等模块；这条不登记
  baseline，超了直接判红。
- **单文件 ratchet**：`src/**/*.cpp`、`src/**/*.h`（不含 build 目录）超过 1500 行必须登记在
  `tools/code-size-baseline.txt`，格式 `路径<TAB>行数`，数字是当前允许的最大行数。
  - 当前行数 > baseline：判红（文件又变大了）。
  - 出现新的 >1500 行文件且未登记：判红。
  - 文件变小：通过，只提示可以收紧 baseline。
  - baseline 里的条目消失或降到 1500 行以下：通过，只提示可以移除。
- **baseline 只降不升**。`--update-baseline` 会按当前行数重写整份文件：变小的收紧、
  消失的移除、变大的会**抬高**——所以它会打印每一处“抬高”，这是需要人类批准的信号。
  为了让 CI 变绿而手工改大数字，等于取消护栏。

为什么用 ratchet 而不是一次砍到 1500：仓库里仍有一批历史大文件（PropertiesPanel、
TimelinePanel、MainWindow 等）。一刀切会让 `main` 立刻全红，实际结果通常是被
`continue-on-error` 或注释掉；ratchet 冻结现状，只禁止继续变大，并允许后续每次拆文件时
用 `--update-baseline` 把上限一路压下去，最终清空 baseline。

CI 的 `code-size` job 只做文本统计，不装 Qt、不构建，**阻塞合并**。

## 已知限制

- CI 的三道 gate 是 `code-size`（文本统计，阻塞合并）、`build-test`（Qt 6.7.3 + 全量
  ctest，并预装 Chrome 让 HTML 图层用例真跑）和 `release`（Release 构建 + 交付检查）；
  像素/视频相关的人工比对仍只在本地做。
- 没有任何 Qt WebSockets 模块，走内置 RFC 6455 客户端这条分支。
- 没有 Chrome/Chromium 时，`html2` 的浏览器相关用例会自行 SKIP；HTML 图层回归只在
  装了浏览器的机器上有效。
- 本机装 Qt 6.6/6.7 会卡在 aqtinstall 3.3.0 + Python 3.14 的下载校验
  （同一份 7z 反复报 checksum 不符，直接 curl 同一 URL 的哈希是对的）。
  默认用发行版自带 ≥ 6.5 的 Qt，或用 `--qt 6.7.3` 单独装到 `build/qt/`。
