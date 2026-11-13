# `src/tests` 自检清单与 CTest 边界（issue #105）

`src/tests` 里有 45 个 `--*-test` 入口，但它们不是同一类东西：一部分是交付壳的行为测试，
一部分是开发机的性能测量、截图和演示工具。把所有入口一次性塞进 CTest 会让默认测试变慢、
把基准波动当回归，也会重新制造“跑了很多但不知道跑了什么”的假绿。

本文件的边界：

- **CTest 只跑下表的 12 个入口**，每个入口一个进程（`pvf.<switch>`），失败会阻塞。
- 其余入口仍是手动开发工具；它们的状态写在“手动/测量”一节。
- 需要浏览器/ffmpeg/git 的入口用退出码 **77** 表示跳过，CTest 注册了
  `SKIP_RETURN_CODE 77`，报表显示 `Skipped`，不会显示 `Passed`。
- CTest 入口统一 `QT_QPA_PLATFORM=offscreen`、`TIMEOUT 300`，按标签分组。

## CTest 覆盖的入口

| CTest 名称 | 入口 | 标签 | 覆盖内容 |
|---|---|---|---|
| `pvf.self-test` | `--self-test` | `self-test` | 测试模式只认精确开关名；`--open /tmp/project-test` 不误入测试模式 |
| `pvf.session-test` | `--session-test` | `self-test;ui` | 创建/打开工程、改名、playhead、旧单文件工程拒绝、无 session 窗口 |
| `pvf.settings-test` | `--settings-test` | `self-test;ui` | 会话设置对话框、AppSettings、工作台服务重建 |
| `pvf.checkpoint-test` | `--checkpoint-test` | `self-test;core` | 检查点创建/恢复、缺失素材记录 |
| `pvf.rename-test` | `--rename-test` | `self-test;ui` | 实体改名与提交 |
| `pvf.panel-layout-test` | `--panel-layout-test` | `self-test;ui` | 工作台面板打开/浮动/停靠/隐藏/几何持久化 |
| `pvf.export-dialog-test` | `--export-dialog-test` | `self-test;ui` | 导出对话框相机/范围/倍率/CRF/编码器选项 |
| `pvf.properties-test` | `--properties-test` | `self-test;ui` | 属性面板变换/内容/关键帧/缓动/解题器控件 |
| `pvf.export-test` | `--export-test` | `self-test;media` | 真实 ffmpeg 导出；缺 ffmpeg/ffprobe 时 exit 77 |
| `pvf.mcp-test` | `--mcp-test` | `self-test;mcp` | MCP 服务启动、工具调用、凭据；测试进程使用临时端口 |
| `pvf.mcp-lock-test` | `--mcp-lock-test` | `self-test;mcp` | AI 写入锁/急停/恢复，面板与时间轴禁用 |
| `pvf.mcp-bridge-test` | `--mcp-bridge-test` | `self-test;mcp` | `pvforge-mcp` CLI 端到端；编译期注入 `PVFORGE_MCP_BINARY` |

运行方式：

```bash
ctest --preset debug -L self-test        # 只跑本表
ctest --preset debug -L ui               # 只跑 UI 组
ctest --preset debug -j8                 # 全量（模块测试 + 本表）
```

## 手动/测量入口（不进 CTest）

这些入口保留在 `pvforge-tests`，需要时手动跑；它们依赖固定硬件/开发机素材，
或本身就是性能测量，不适合当阻塞回归。

### 交付壳 / 会话

- `--welcome-test`：欢迎页/新建工程对话框，内部 `app.exec()` + 定时器，只适合手动。
- `--git-test`：真实 git 写/恢复；缺 git 时 exit 77。

### 时间轴

- `--timeline-test`、`--timeline-bottomup-test`、`--timeline-ui-test`、
  `--timeline-assets-test`、`--timeline-keys-test`、`--timeline-split-test`、
  `--timeline-marker-test`
- `--timeline-edit-e2e`：默认写 `/tmp/pvfe2e/proj`，参数可指定工程目录，属于端到端演示。
- `--timeline-edit-test`：**已知 flaky**，连续跑会出现 `cascade delete failed:`
  （`EditActions::deleteEntities` 返回 false 但 error 为空）。修好前不要注册 CTest。

### 属性 / 解题器 / 素材 / 视口

- `--keyframe-easing-group-test`、`--properties-special-test`、
  `--html-vars-test`、`--text-layout-test`、`--brandwall-create-test`
- `--solver-ui-test`、`--solver-mini-preview-test`、`--solver-preview-freeze-test`、
  `--solver-convert-test`
- `--assets-test`、`--asset-import-test`、`--asset-drag-test`、`--asset-replace-test`、
  `--workbench-assets-test`
- `--gizmo-test`、`--inkscape-handles-test`、`--camera-monitor-test`

### 测量 / 演示（不是测试）

这些入口没有回归阈值，输出的是 profile/截图，不应进入 CTest，也不应计入“测试数量”：

- `--cache-miss-profile`：依赖 `/home/xmb505/视频/PVForge/未命名项目1.pvproj`；
  缺少该工程时 exit 77，其余输出是性能报告。
- `--cache-encoder-bench`：编码器 benchmark，只打印数据。
- `--perf-monitor-test`、`--perf-flame-test`：性能面板/火焰图诊断。
- `--solver-playback-perf-test`：播放性能测量。

**删除建议**：上述测量入口如果长期没人跑，应从 `TestRegistry` 移除，改成独立
`tools/` 脚本；在负责人确认前先保留为手动入口，不注册 CTest，也不再计入测试指标。

## 维护规则

1. 新入口先在 `TestRegistry` 注册，再决定是否加入 `PVF_SELF_TEST_CASES`。
2. 只把“能在无用户配置、无网络、无固定端口的机器上稳定跑完”的入口加进 CTest。
3. 需要外部工具时，检查失败必须 `exit 77`，并在 `add_test` 里保留
   `SKIP_RETURN_CODE 77`；不要 `return 0`。
4. 同一个 CTest 进程只传一个 `--*-test`：`Registry::run()` 只执行第一个命中的开关。
5. 删除入口时同步删掉本文件和 `CMakeLists.txt` 里的 CTest 项。
