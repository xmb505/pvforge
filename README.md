# PVForge

**用 Mine-imator 的工作流做 2D 音乐 PV 的桌面编辑器。** 纯 C++20 + Qt 6，工程是可读的 `.pvproj/` 目录（JSON 文本 + 素材）。

> 当前版本：**0.3.0 预览（2026-11-12）**。旧 UI 的布局、面板、快捷键和交互保留，后端已切到新实体模型。Linux x86_64 优先。

## 下载

[v0.3.0-preview](https://github.com/xmb505/pvforge/releases/tag/v0.3.0-preview) 提供 Linux x86_64 的 `pvforge` 与 `pvforge-mcp`（MCP stdio 桥，可选）。

运行需要系统 Qt 6.5+ 运行库（Widgets、Concurrent、Multimedia、Network、Sql）与 FFmpeg 8 运行库；发布包在 Ubuntu 25.10 / Qt 6.9 / FFmpeg 8.0 上构建，建议使用同等或更新版本。其他发行版请从源码构建。

```bash
chmod +x pvforge pvforge-mcp
./pvforge
```

## 亮点

- **沿用旧 UI**：视口、时间轴、属性面板、工作台、监视器、菜单和快捷键保持原有交互。依据：`src/app/`、`src/ui/`
- **新实体模型**：实体带 UUID、父级、时间范围、图层行、变换、内容和关键帧；工程拆成 `project.json` 与 `entities/*.json`。依据：`src/model/Entity.h`、`src/model/Project.h`、`src/store/Store.h`
- **时间轴与关键帧**：逐属性轨道、自动关键帧、缓动预设与自定义贝塞尔、关键帧拖动与复制、父级坐标与图层树、图表页曲线、波形页、markers。依据：`src/ui/TimelinePanel.cpp`、`src/ui/CurveEditor.cpp`
- **求解五效果**：重力、公转、自转、黑洞、螺旋；整段预计算并缓存，时间轴显示停止色带，属性面板提供轨迹和初速度手柄。依据：`src/common/Types.h`、`src/solver/`
- **工作台缓存**：不含素材的帧用内置 QOI，含图片/视频/HTML 的帧用 WebP；内存 LRU 加磁盘预算，后台 1–4 线程异步编码。依据：`src/workbench/Cache.h`、`src/workbench/Qoi.h`
- **性能监视**：独立窗口显示 FPS、p50/p95/p99、阶段耗时、缓存和系统指标，可录制并导出报告，火焰图可下钻。依据：`src/ui/PerfMonitorWindow.cpp`、`src/perf/`
- **素材与视频**：图片、视频、音频导入，视频抽帧缩略图与内嵌音轨拆分，后台生成预览代理。依据：`src/media/MediaImporter.cpp`、`src/media/VideoProxy.cpp`
- **文字 / 矢量 / HTML**：多行与竖排排版、描边、漫画文本框；9 种矢量形状；`.pvhtml` 组件包。依据：`src/core/TextBlock.cpp`、`src/ui/WorkbenchPanel.cpp`、`src/assets/Pvhtml.h`
- **摄像机与监视器**：摄像机关键帧、取景框、草稿/高质量监视器、弹出窗口、临摹参考视频。依据：`src/ui/CameraMonitor.cpp`、`src/ui/Viewport.cpp`
- **MP4 导出**：按摄像机视角导出 H.264 + AAC，libx264 软件编码或 VAAPI 硬件编码，可选分辨率与帧率倍率、CRF、preset；导出时有实时预览、进度和取消。依据：`src/export2/Export.cpp`、`src/app/MainWindow.cpp`
- **工程体验**：原子保存、自动保存、播放头记忆、内存撤销/重做、可选 git 冷备与 Git 历史面板。依据：`src/store/Store.h`、`src/core/CheckpointManager.h`
- **MCP**：应用内启动仅监听 `127.0.0.1` 的 MCP 服务，`pvforge-mcp` 提供 stdio 桥；可读写实体、关键帧、求解和预览。依据：`src/mcp2/`、`src/mcp/`

## 构建

- CMake 3.21+、C++20 编译器（GCC 或 Clang）、Ninja（可选）
- Qt 6.5+：Core、Gui、Widgets、Concurrent、Multimedia、Network、Sql；WebSockets 可选（缺失时使用内置客户端）
- FFmpeg 开发库：libavformat、libavcodec、libavutil、libswscale、libswresample
- 可选运行依赖：`ffmpeg` 命令行（视频预览代理）、git（版本历史与冷备）、Chrome / Edge / Chromium（HTML 图层）、VAAPI 设备（硬件编码）

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/pvforge
```

产物是 `build/pvforge` 与 `build/pvforge-mcp`。`ctest` 需要 `BUILD_TESTING`，顶层默认开启。

## 快速开始

1. 启动后新建项目（`Ctrl+N`），填写名称、位置、分辨率和帧率；分辨率与帧率创建后锁定。
2. 点视口左上角立方体打开工作台，从「媒体 / 文字 / 文本框 / 矢量 / 特殊 / 摄像机」创建对象；图片、视频、音频也可从素材库导入。
3. 在视口拖动摆位，右侧属性面板改变换、透明度和父级；自动关键帧默认开启。
4. 在时间轴拖动播放头并打关键帧；「图表」页调曲线，「波形」页对音频。
5. 创建摄像机并摆好取景框，用右下角监视器检查最终画面。
6. `空格` 播放，`Ctrl+S` 保存 `.pvproj` 目录，`Ctrl+E` 选择摄像机导出 MP4。

## 测试

```bash
QT_QPA_PLATFORM=offscreen ./build/pvforge --self-test
QT_QPA_PLATFORM=offscreen ./build/pvforge --export-test
QT_QPA_PLATFORM=offscreen ./build/pvforge --mcp-test
ctest --test-dir build --output-on-failure
```

`src/main.cpp` 里还有按功能分组的离屏自检参数；完整清单以源码为准。

## 已知限制

- Linux x86_64 是当前验证目标，Windows/macOS 未验证；性能监视的进程与系统指标只读取 Linux `/proc`。
- HTML 图层依赖本机安装 Chrome、Edge 或 Chromium；找不到浏览器时 HTML 帧会失败。
- 导出必须选择一台摄像机；默认 libx264 软件编码，长工程耗时较长；VAAPI 需要 `/dev/dri` 和带 `h264_vaapi` 的 FFmpeg。
- 视频预览代理由外部 `ffmpeg` 生成；缺少时导出仍可用，但长视频随机拖动会更慢。
- 竖排文字、气泡和富文本仍有未覆盖的排版细节。

## 文档

- [功能详情](docs/FEATURES.md)
- [操作手册](docs/USAGE.md)
- [实体模型](docs/ENTITY_MODEL.md)
- [工作台缓存](docs/WORKBENCH_CACHE.md)
- [MCP 工具](docs/MCP_TOOLS.md)
- [里程碑](docs/MILESTONE.md)
- [当前状态与交接](docs/STATUS.md)
- [架构与设计](docs/DESIGN.md)

## 参考与致谢

交互和工作流参考了这些软件（仓库不包含它们的代码或资产）：

- [Mine-imator](https://www.mineimator.com/)：对象树、时间轴、关键帧、工作台、素材库、缓动曲线
- [Blender](https://www.blender.org/)：资源打包流程、对象与摄像机心智模型
- [OBS Studio](https://obsproject.com/)：监视器弹出/停靠的窗口交互
- [After Effects](https://www.adobe.com/products/aftereffects.html)：单属性轨道的精细关键帧交互参考
- [LibreOffice](https://www.libreoffice.org/)：文本框、气泡和属性对话框的布局与交互
- [Krita](https://krita.org/)：文字排版与画布属性对话框的布局参考
- [qBittorrent](https://www.qbittorrent.org/)：设置页的左侧分类与右侧分页结构
- [FFmpeg](https://ffmpeg.org/)：视频/音频解码、预览代理与导出编码

## 许可

源码采用 **MIT License**，见 [LICENSE](LICENSE)。

依赖许可证：

- Qt 6：LGPLv3 或商业许可
- FFmpeg：LGPL 2.1+ 或 GPL，取决于构建选项

分发二进制时请同时遵守上述依赖的许可证。本项目只参考 Mine-imator 的动画工作流，不包含 Mine-imator 代码或任何 Minecraft 资产。
