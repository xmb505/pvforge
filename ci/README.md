# CI 与本地验证

一套命令，CI（`.github/workflows/ci.yml`）和本地共用。构建系统本身的选项在顶层
`CMakeLists.txt`，这里只负责依赖与调用方式。

## 依赖

```bash
ci/install-deps.sh              # apt：Ninja、ccache、FFmpeg 开发库、Qt 运行/构建依赖、Xvfb
ci/install-deps.sh --qt 6.7.3   # 额外用 aqtinstall 装 Qt 到 build/qt/6.7.3/linux_gcc_64
```

aqt 的 Linux 桌面架构名随版本变化：**6.6 及更早是 `gcc_64`，6.7 起是
`linux_gcc_64`**（`aqt list-qt linux desktop --arch <version>`）。脚本会自己查，
CI 的 matrix 里两个版本各写各的架构。

- **Ubuntu 24.04 自带的 Qt 是 6.4.2**（`qt6-base-dev`），低于 `CMakeLists.txt` 里的
  `find_package(Qt6 6.5 REQUIRED ...)`，直接 configure 会失败。本地必须用 aqt 装
  6.6.x/6.7.x（或发行版自带 ≥ 6.5 的 Qt，例如 26.04 的 6.10）。
- CI 里 Qt 由 `jurplel/install-qt-action` 安装，脚本只装系统依赖，两边共用同一份 apt 列表。
- FFmpeg：CI 用 Ubuntu 24.04 的 6.1 开发库（libavformat 60），发布包是 FFmpeg 8；
  两者都在 `pkg_check_modules` 的同一套 API 上，但版本差异只在发布包里验证。

## 构建与测试

```bash
cmake --preset debug                  # build/debug，Debug + BUILD_TESTING/REFERENCE/SELF_TESTS=ON
cmake --build --preset debug -j 8
ctest --preset debug                  # 已带 QT_QPA_PLATFORM=offscreen 与 --output-on-failure

cmake --preset release                # build/release，Release，不构建测试/参考壳
cmake --build --preset release -j 8   # 产物 build/release/{pvforge,pvforge-mcp}

cmake --preset tests                  # build-coord 风格：-O0 -g，全量测试（含参考壳）
cmake --preset reference              # 只想构建参考壳时的 Debug preset
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

`CMakePresets.json` 是纯 JSON，不能写注释，所以记在这里。顶层 `CMakeLists.txt` 有三类目标：

- **delivery**：`pvforge`、`pvforge-mcp`，以及它们实际链接的模块（含
  `pvf_next_document`、`pvf_next_services`、`pvf_next_properties` 的
  `SolverPreviewWidget` 等、`pvf_next_timeline` 的 `MarkerDialog` 等）。
- **reference**：`src/next/app` 的 `NextWindow`（`pvf_next_app`）与
  `src/next/viewport` 的 `ViewportWidget`（`pvf_next_viewport`）。参考壳只在
  `PVFORGE_BUILD_REFERENCE=ON` 时参与构建。
- **tests**：`pvf_*_tests` 可执行文件。参考壳测试同时受 `BUILD_TESTING` 与
  `PVFORGE_BUILD_REFERENCE` 控制，其余测试只看 `BUILD_TESTING`。

对应选项：

- `PVFORGE_BUILD_SELF_TESTS`：把 `src/main.cpp` 里的 `--*-test` 自检开关编进/编出。
  Debug 默认 ON，Release 默认 OFF。
- `PVFORGE_BUILD_REFERENCE`：是否构建 `src/next/app` 的参考壳、`src/next/viewport`
  与它的窗口测试（`next.window_shell`、`next.media_services`、
  `next.playback_controller`、`next.workbench_integration`、`next.camera_monitor`、
  `viewport.tests`）。**默认 OFF**；Debug/开发测试需要这些用例时显式打开。

两个选项正交。`debug` / `tests` / `reference` preset 都打开 reference 与 self-tests；
`release` 全部关闭。不用 preset 时默认是 Release 语义的裁剪构建，只有显式传
`-DPVFORGE_BUILD_REFERENCE=ON` 才会编参考壳。

## 已知限制

- CI 只跑 Linux x86_64 + offscreen；没有 GPU，像素/视频相关断言依赖软件光栅。
- 没有任何 Qt WebSockets 模块，走内置 RFC 6455 客户端这条分支。
- 没有 Chrome/Chromium，`html2` 的浏览器相关用例会自行 SKIP；HTML 图层回归只在
  装了浏览器的机器上有效。
- 运行环境是 GitHub hosted runner（4 vCPU），全量构建 + 323 个测试约 15–25 分钟。
- 本机（Qt 6.10 + 软件光栅，`-j 8`）实测 323 个用例里稳定失败的是
  `viewport.tests`、`next.media_services`、`next.camera_monitor`（像素取色 / 固定
  MCP 端口 / entityCommits 计数），CI 里单独跑且 `continue-on-error`；
  `html2.asyncRequestLowRes`、`html2.lowResPreemption` 只在并行负载高时偶发失败，
  CI 用 `--repeat until-pass:2` 兜住，不排除。这三个用例正在被单独修，修好后把
  workflow 里的 `KNOWN_FLAKY` 和各步骤的 `--exclude-regex`/`-R` 参数删掉即可收窄白名单。
- 本机装 Qt 6.6/6.7 会卡在 aqtinstall 3.3.0 + Python 3.14 的下载校验
  （同一份 7z 反复报 checksum 不符，直接 curl 同一 URL 的哈希是对的），
  所以 6.6.3/6.7.3 的**编译**只在 CI 的 Ubuntu 24.04 + Python 3.12 环境里首次验证。
