# `src/tests` 自检清单与 CTest 边界（issue #110）

`src/tests` 注册了 45 个 `--*` 自检入口。**除 5 个测量/演示入口外，其余 40 个全部注册进
CTest**，每个入口一个进程（`pvf.<switch>`），失败会阻塞。CI 的 `build-test` job 会构建
Debug 二进制并执行 `ctest -L self-test`。

需要浏览器/ffmpeg/git 的入口用退出码 **77** 表示跳过；CTest 注册了
`SKIP_RETURN_CODE 77`，报表显示 `Skipped`，不会显示 `Passed`。所有入口统一
`QT_QPA_PLATFORM=offscreen`、`TIMEOUT 300`（端到端与导出为 600），并持有
`pvforge_mcp_port` 资源锁，避免 `ctest -j` 时争抢固定 MCP 端口。

## CTest 覆盖的入口（40）

### 核心 / 会话（`self-test;core`）

| CTest 名称 | 入口 | 覆盖内容 |
|---|---|---|
| `pvf.self-test` | `--self-test` | 测试模式只认精确开关名；`--open` 不误入测试模式 |
| `pvf.checkpoint-test` | `--checkpoint-test` | 检查点创建/恢复、缺失素材记录 |
| `pvf.git-test` | `--git-test` | 真实 git 写/恢复；缺 git 时 exit 77 |

### UI / 交付壳（`self-test;ui`）

| CTest 名称 | 入口 | 覆盖内容 |
|---|---|---|
| `pvf.session-test` | `--session-test` | 创建/打开工程、改名、playhead、旧单文件工程拒绝 |
| `pvf.welcome-test` | `--welcome-test` | 欢迎页/新建工程对话框流程 |
| `pvf.settings-test` | `--settings-test` | 会话设置对话框、AppSettings、工作台服务重建 |
| `pvf.rename-test` | `--rename-test` | 实体改名与提交 |
| `pvf.panel-layout-test` | `--panel-layout-test` | 工作台面板打开/浮动/停靠/隐藏/几何持久化 |
| `pvf.properties-test` | `--properties-test` | 属性面板变换/内容/关键帧/缓动/求解器控件 |
| `pvf.keyframe-easing-group-test` | `--keyframe-easing-group-test` | 同帧多轨道整体缓动 |
| `pvf.properties-special-test` | `--properties-special-test` | 特殊内容编辑（漫画/线条/点阵/品牌墙） |
| `pvf.html-vars-test` | `--html-vars-test` | HTML 变量面板与提交 |
| `pvf.text-layout-test` | `--text-layout-test` | 多行/竖排文字排版 |
| `pvf.brandwall-create-test` | `--brandwall-create-test` | 品牌墙创建与参数 |
| `pvf.export-dialog-test` | `--export-dialog-test` | 导出对话框相机/范围/倍率/CRF/编码器选项 |

### 时间轴（`self-test;ui`）

| CTest 名称 | 入口 | 覆盖内容 |
|---|---|---|
| `pvf.timeline-test` | `--timeline-test` | 行/片段显示、播放头、点击选中、私有时间线 |
| `pvf.timeline-bottomup-test` | `--timeline-bottomup-test` | 自下而上的轨道占用、空白行拖动、私有时间线 |
| `pvf.timeline-ui-test` | `--timeline-ui-test` | 时间轴缩放/滚动/刻度/行交互 |
| `pvf.timeline-assets-test` | `--timeline-assets-test` | 音频/素材行元数据与峰值 |
| `pvf.timeline-edit-test` | `--timeline-edit-test` | 拖动/裁剪/换行/换父级/级联删除 |
| `pvf.timeline-keys-test` | `--timeline-keys-test` | 关键帧拖动/复制/删除/合并菱形 |
| `pvf.timeline-split-test` | `--timeline-split-test` | S 切分实体与关键帧 |
| `pvf.timeline-marker-test` | `--timeline-marker-test` | marker 增删改与绘制 |
| `pvf.timeline-edit-e2e` | `--timeline-edit-e2e` | 在 `QTemporaryDir` 内自建工程，端到端编辑/重开 |

### 素材（`self-test;ui`）

| CTest 名称 | 入口 | 覆盖内容 |
|---|---|---|
| `pvf.assets-test` | `--assets-test` | 素材导入/搜索/标签/缩略图/布局 |
| `pvf.asset-import-test` | `--asset-import-test` | 图片/视频/音频/HTML 导入与去重 |
| `pvf.asset-drag-test` | `--asset-drag-test` | 素材拖到视口/时间轴 |
| `pvf.asset-replace-test` | `--asset-replace-test` | 换素材兼容组与越界 key 处理 |
| `pvf.workbench-assets-test` | `--workbench-assets-test` | 工作台素材页签与创建入口 |

### 求解 / 视口（`self-test;ui`、`self-test;core`）

| CTest 名称 | 入口 | 覆盖内容 |
|---|---|---|
| `pvf.solver-ui-test` | `--solver-ui-test` | 求解段 UI、轨迹、初速度手柄 |
| `pvf.solver-mini-preview-test` | `--solver-mini-preview-test` | 属性面板小型求解预览 |
| `pvf.solver-preview-freeze-test` | `--solver-preview-freeze-test` | 预览冻结/重算 |
| `pvf.solver-convert-test` | `--solver-convert-test` | 普通 key 转求解关键帧 |
| `pvf.gizmo-test` | `--gizmo-test` | Gizmo 拖动/吸附/取消/缓存绕过与刷新 |
| `pvf.inkscape-handles-test` | `--inkscape-handles-test` | Inkscape 四角弯箭头旋转手柄 |
| `pvf.camera-monitor-test` | `--camera-monitor-test` | 相机监视器/取景框/临摹 |

### 导出 / MCP

| CTest 名称 | 入口 | 标签 | 覆盖内容 |
|---|---|---|---|
| `pvf.export-test` | `--export-test` | `self-test;media` | 真实 ffmpeg 导出；缺 ffmpeg/ffprobe 时 exit 77 |
| `pvf.mcp-test` | `--mcp-test` | `self-test;mcp` | MCP 服务启动、工具调用、凭据；临时端口 |
| `pvf.mcp-lock-test` | `--mcp-lock-test` | `self-test;mcp` | AI 写入锁/急停/恢复 |
| `pvf.mcp-bridge-test` | `--mcp-bridge-test` | `self-test;mcp` | `pvforge-mcp` CLI 端到端 |

运行方式：

```bash
ctest --preset debug -L self-test        # CI 跑的就是这一组
ctest --preset debug -L ui               # 只跑 UI 组
ctest --preset debug -LE slow            # 跳过较重的用例
ctest --preset debug -j8                 # 全量（模块测试 + 本表）
```

## 手动/测量入口（不进 CTest，5 个）

这些入口没有回归阈值，输出的是 profile/截图/数据，不应计入“测试数量”：

- `--cache-miss-profile`：需要 `PVFORGE_PROFILE_PROJECT=<工程目录>`；未设置或不存在时 exit 77。
- `--cache-encoder-bench`：编码器 benchmark，只打印数据。
- `--perf-monitor-test`、`--perf-flame-test`：性能面板/火焰图诊断。
- `--solver-playback-perf-test`：播放性能测量。

## 维护规则

1. 新入口先在 `TestRegistry` 注册；行为断言类默认加入 `PVF_SELF_TEST_CASES` 和 CI。
2. 只有“没有行为断言、结果随机器波动”的测量/演示入口可以留在手动区，并在本文件登记。
3. 需要外部工具时，检查失败必须 `exit 77`，并在 `add_test` 里保留
   `SKIP_RETURN_CODE 77`；不要 `return 0`。
4. 同一个 CTest 进程只传一个 `--*-test`：`Registry::run()` 只执行第一个命中的开关。
5. 删除入口时同步删掉本文件和 `CMakeLists.txt` 里的 CTest 项。
