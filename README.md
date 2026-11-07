# PVForge

用 Mine-imator 的动画工作流做 2D 音乐 PV 的桌面合成器。

当前是 M0.2：**工作台创建实体 → 打关键帧 → 播放时动起来**。

## 构建

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/pvforge
```

依赖：Qt 6.5+、FFmpeg（libavformat/libavcodec/libswscale）、CMake、Clang/Ninja。

自检模式：

```bash
QT_QPA_PLATFORM=offscreen ./build/pvforge --self-test
QT_QPA_PLATFORM=offscreen ./build/pvforge --workbench-test
```

## 操作

| 操作 | 方式 |
|---|---|
| 打开工作台 | 视口左上角立方体按钮 |
| 创建工作台实体 | 左侧选类型，右侧填参数，点「创建」 |
| 关闭工作台 | 点击浮层外部、Esc，或再点立方体按钮 |
| 移动实体 | 左键拖动（自动在当前帧打帧） |
| 改变换/透明度 | 右侧属性面板（自动打帧） |
| 显式打全属性关键帧 | 属性面板「在当前帧打关键帧」 |
| 拖播放头 | 点时间轴刻度区左右拖 |
| 播放/暂停 | `空格` |
| 视口缩放/平移 | 滚轮 / 中键拖动 |
| 关掉自动关键帧 | 工具栏「自动关键帧」 |

## 实体类型

| 类型 | 能力 |
|---|---|
| 图片 | PNG/JPG/WebP/BMP，导入后按画布比例适配 |
| 视频 | MP4/WebM/MOV/MKV，FFmpeg 解码首帧显示；逐帧播放待接入 |
| 文字 | 内容、字体、字号、颜色、对齐，创建后属性面板可继续改 |
| 矢量 | 矩形/圆形/直线，宽高、填充、描边、描边宽度 |

## 已实现

- 视口内工作台浮层：左侧类型列表、右侧表单、底部创建按钮、滑入动画、点外部关闭
- 四类实体的创建、渲染、选中、拖动
- 位置、旋转、缩放、透明度属性与逐属性关键帧
- 线性插值、时间轴刻度与关键帧标记、播放头拖动
- 30 fps 播放与循环
- 自动关键帧（默认开启）
- 视频首帧解码
- 文字与矢量渲染，属性面板按类型切换分组

## 路线

保存/加载 → 缓动曲线与关键帧编辑 → 视频逐帧播放 → 音频波形/频谱 → 后处理 → MP4 导出。

完整设计见 [docs/DESIGN.md](docs/DESIGN.md)。
