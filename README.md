# PVForge

**学过 Mine-imator，就能画 PV。**

用 Mine-imator 的工作流做 2D 音乐 PV 的桌面编辑器。纯 C++20 + Qt 6，工程格式是可读的 `.pvproj` JSON。

> 当前状态：**M3 / 0.2.0**。时间轴、关键帧、素材、工作台、撤销、保存、摄像机、监视器、音频、自动保存、文字排版、文本框、特殊工具都已可用；**还没有 MP4 导出**。

## 亮点

- **Mine-imator 式时间轴**：逐属性关键帧、自动关键帧、帧精确刻度、单帧步进、关键帧缓动与拖动
- **完整图层树**：文件夹、父级坐标、锁定/隐藏、多选拖动、整棵子树复制粘贴
- **摄像机与监视器**：摄像机关键帧、取景框、临摹参考视频、手机 HTTP/RTP 串流、独立弹出窗口
- **文字与漫画**：多行/竖排、行距字距、文字描边、云朵/椭圆/爆炸气泡、可拖动尖角
- **特殊工具**：放射状线条、快速流过、漫画点阵、品牌瓷砖墙
- **音频与视频**：FFmpeg 软件解码、视频内嵌音轨自动拆分、波形时间轴、预览代理
- **工程体验**：自动保存、播放头记忆、更换素材、全局撤销/重做、性能监视
- **性能**：文字/气泡/点阵按参数缓存，大量实体和 0 透明度对象不会拖慢播放

## 构建

依赖：

- CMake 3.21+
- C++20 编译器（Clang 或 GCC）
- Qt 6.5+：Widgets、Concurrent、Multimedia
- FFmpeg 开发库：libavformat、libavcodec、libavutil、libswscale、libswresample
- `ffmpeg` 命令行：可选，用于视频预览代理和 RTP 串流
- Ninja（可选）

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/pvforge
```

Linux 优先；Windows/macOS 在代码上没有硬依赖，但未验证。

## 快速开始

1. 启动后新建项目，设置分辨率、帧率和背景色
2. 点视口左上角立方体打开工作台，创建图片/视频/文字/矢量
3. 在视口里拖动摆位；右侧属性面板改变换/透明度
4. 在工作台创建摄像机，拖取景框摆好构图
5. 右下角监视器看最终画面；拖标题条左侧点阵可弹出到副屏
6. 空格播放，`Ctrl+S` 保存为 `.pvproj`

## 文档

- [功能详情](docs/FEATURES.md)：完整功能清单、实体类型、工程格式
- [操作手册](docs/USAGE.md)：所有快捷键和日常操作
- [里程碑与已知限制](docs/MILESTONE.md)
- [设计文档](docs/DESIGN.md)

## 测试

49 个离屏自检。完整模式列表和验收项见 [docs/MILESTONE.md](docs/MILESTONE.md)。

```bash
QT_QPA_PLATFORM=offscreen ./build/pvforge --self-test
QT_QPA_PLATFORM=offscreen ./build/pvforge --special-test
QT_QPA_PLATFORM=offscreen ./build/pvforge --monitor-feature-test
```

## 路线

**音频属性撤销 → 频谱对象 → MP4 导出**。

## 许可

源码采用 **MIT License**，见 [LICENSE](LICENSE)。

依赖许可证：

- Qt 6：LGPLv3 或商业许可
- FFmpeg：LGPL 2.1+ 或 GPL，取决于构建选项

分发二进制时请同时遵守上述依赖的许可证。本项目只参考 Mine-imator 的动画工作流，不包含 Mine-imator 代码或任何 Minecraft 资产。
