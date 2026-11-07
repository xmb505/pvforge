# PVForge 设计文档

> 工作名 PVForge，可随时更名。仓库 `pvforge`。

## 1. 定位

用 Mine-imator 的动画工作流做 2D 音乐 PV 的桌面合成器。

目标用户是已经会 3D 动画软件（Mine-imator、Blender）的 PV 制作者。核心卖点是**不用学 After Effects**：场景列表、时间轴、关键帧、相机、后处理，操作习惯与 3D 动画软件一致，但渲染是纯 2D 正交投影，出片对象是二次元 PV。

一句话：**把 Mine-imator 的动画家心智模型，搬到 2D PV 上。**

## 2. v1 范围

### 做

| 能力 | 说明 |
|---|---|
| 图片图层 | PNG/JPG/WebP，锚点、裁剪、着色、透明度 |
| 视频图层 | FFmpeg 解码首帧；逐帧播放、帧精确 seek、循环、透明通道在后续里程碑 |
| 文字图层 | 内容、字体、字号、颜色、对齐；静态文字，不做逐字动画 |
| 矢量图层 | 矩形、圆形、直线；宽高、填充、描边 |
| 空对象/组 | 父子层级，用于整体变换 |
| 变换动画 | 位置、旋转、缩放、锚点、透明度，逐属性关键帧 |
| 缓动 | 预设（线性/缓入/缓出/缓入缓出）+ 自定义三次贝塞尔 |
| 曲线编辑 | 关键帧曲线视图，可拖拽控制点 |
| 相机 | 平移、缩放、旋转、震动（振幅/频率/衰减），支持关键帧 |
| 音频轨 | 导入音频，底部波形显示，精确同步播放 |
| 频谱对象 | FFT 柱状/波形可视化，频段数、平滑、配色、方向可调 |
| 后处理 | Bloom、色差、色彩校正（亮度/对比度/饱和度/自然饱和度）、颗粒、暗角、故障、扫描线 |
| 实时预览 | 视口内播放，音频同步，所见即所得 |
| MP4 导出 | 离线逐帧渲染，H.264，音频混流，进度与取消 |
| 工程文件 | JSON，可读可 diff，保存/加载/最近文件 |
| 撤销重做 | 基于命令栈，覆盖所有编辑操作 |

### 不做（v1 明确排除）

- 歌词分段与逐字文字动画（文字对象本身已支持静态内容）
- BPM 检测与节拍网格
- 图片序列帧素材（视频素材已覆盖该需求）
- PSD 分层导入
- 2D 骨骼/网格变形（Spine/Live2D）
- 口型同步
- 3D 渲染、Minecraft 方块/皮肤/世界导入
- 透明通道视频导出
- 多场景/多摄像机剪辑

## 3. 核心概念

### 3.1 Project

一个 PV 工程。字段：分辨率、帧率、背景色、实体列表、音频轨。**没有固定时长**：时间轴长度取最长素材（视频/音频）与最后一个关键帧的最大值。空工程长度为 0，时间轴开放，播放头可无限前进；只有存在内容时才在长度处循环。

### 3.2 Entity（对象）

所有可动画对象的基类，扁平存储、父子引用：

```
Entity
├─ id            int，工程内唯一
├─ type          Image | Video | Text | Vector（后续扩展 Audio/Spectrum/Camera/Group）
├─ name          QString
├─ parent        EntityId，-1 为根
├─ visible       bool
├─ tracks        QHash<QString, Track>   属性名 → 轨道
├─ values        QHash<QString, QVariant> 无关键帧时的静态值
└─ data          QVariantMap             类型专属数据（素材路径等）
```

2D 没有 Z 轴。绘制顺序 = 时间轴列表顺序，父节点先于子节点，同层按列表顺序。需要调整前后关系时拖动时间轴行。

### 3.3 属性与轨道

每个属性独立一条轨道。属性名使用点分命名，例如：

- `transform.x`、`transform.y`
- `transform.rotation`
- `transform.scaleX`、`transform.scaleY`
- `transform.anchorX`、`transform.anchorY`
- `opacity`
- `camera.zoom`、`camera.shake.amp`、`camera.shake.freq`、`camera.shake.decay`
- `spectrum.bands`、`spectrum.smoothing`
- `post.bloom.intensity`、`post.ca.amount`、`post.grain.strength` …

属性注册表定义：名称、值类型、默认值、UI 范围、是否可关键帧。轨道只存实际动画过的属性，静态值走 `values`。

给对象一键打关键帧时，把全部可动画属性的当前值写入关键帧，行为上接近 Mine-imator 的整对象关键帧；单属性轨道展开则提供 AE 式精细控制。

### 3.4 Keyframe

```
frame      double   帧号，允许小数
value      QVariant float / vec2 / color / bool / string
easing     Bezier   出向缓动（本关键帧到下一关键帧）
```

轨道求值：找到目标帧前后两个关键帧，按 easing 归一化插值；首尾外推为常量。颜色用线性插值，布尔/字符串取最近关键帧。

### 3.5 素材库

工作台每次创建实体时，把它的参数捕获成模板（Template）：图片/视频记路径，文字/矢量记参数。素材库面板列出模板，可预览、再次创建实体、删除。模板随工程保存，等同 Mine-imator 的 Library 条目。

### 3.6 时间

内部以帧为单位，整数帧用于显示与导出，小数帧用于插值。`fps` 默认 30，可在工程设置中改。预览时钟以音频为主时钟，导出以帧为准。

## 4. 架构

```
src/
├─ core/      数据模型：Project、Entity、Track、Keyframe、Easing、序列化
├─ anim/      求值器：给定帧号输出场景快照
├─ render/    渲染器：纹理、批绘制、FBO、后处理链、shader
├─ media/     解码器：图片、视频、音频（FFmpeg）
├─ audio/     音频引擎：PCM 播放、重采样、波形、FFT
├─ export/    离线渲染与 MP4 编码
├─ ui/        Qt Widgets 面板：视口、属性、素材库、项目属性、时间轴、工作台
└─ app/       入口、主窗口、菜单、快捷键、撤销栈
```

### 4.1 单向数据流

```
Project (数据)
   │  EditorCommand (撤销栈)
   ▼
Evaluator::evaluate(project, frame) → RenderSnapshot
   │
   ├─ 预览：Renderer 画到 QOpenGLWidget
   └─ 导出：Renderer 画到 FBO → 编码器
```

预览与导出共用同一条求值/渲染路径，保证所见即所得。UI 不直接改渲染状态。

### 4.2 RenderSnapshot

求值器输出扁平绘制列表：

```
struct DrawItem {
    EntityId id;
    QMatrix4x4 worldTransform;
    Texture* texture;
    QRectF sourceRect;
    float opacity;
    BlendMode blend;
    ShaderParams params;   // tint、亮度、对比度等
};
struct FrameSnapshot {
    QVector<DrawItem> items;
    CameraState camera;
    PostFxParams post;
    QColor background;
};
```

渲染器只消费快照，不感知工程结构。

## 5. 渲染管线

OpenGL 3.3 Core，`QOpenGLWidget`，sRGB 帧缓冲。

1. 场景上传到场景 FBO（RGBA8，分辨率 = 工程分辨率）
2. Bloom：亮度阈值 → 分离高斯模糊（多级降采样）→ 合成
3. 色差：RGB 通道径向偏移
4. 色彩校正：亮度/对比度/饱和度/自然饱和度（LUT 可选）
5. 颗粒 / 暗角 / 扫描线 / 故障：单 pass 叠加
6. 输出 FBO → 视口缩放显示或编码器取像素

视频纹理：FFmpeg 解码 YUV420P → 上传三个平面 → shader 转 RGB；带 alpha 的编码走 RGBA 路径。图片纹理：QImage 上传，sRGB。

## 6. 音频与频谱

- 解码：FFmpeg 解全部音频到内存（48kHz 立体声 float），PV 时长通常 < 10 分钟，内存可控
- 播放：QAudioSink 推送 PCM，位置作为主时钟；播放头 = 音频位置 × fps
- 波形：对 PCM 做 min/max 降采样，缓存成多级金字塔，时间轴按当前缩放取层
- 频谱：对播放头前 2048 采样加 Hann 窗做 FFT，取对数频段，做时间平滑；频谱对象每帧读取频段数组生成顶点
- 导出音频：直接取 PCM 编码进 MP4

## 7. 工程格式

`.pvproj`，UTF-8 JSON：

```json
{
  "format": "pvforge-project",
  "version": 1,
  "width": 1920,
  "height": 1080,
  "fps": 30,
  "background": "#101014",
  "entities": [
    {
      "id": 1,
      "type": "image",
      "name": "背景",
      "parent": -1,
      "data": { "path": "assets/bg.png" },
      "values": { "transform.x": 0.0, "opacity": 1.0 },
      "tracks": {
        "transform.scaleX": [
          { "frame": 0, "value": 1.0, "easing": [0.42, 0, 0.58, 1] }
        ]
      }
    }
  ]
}
```

素材路径相对工程文件保存。v1 不打包素材。导出视频不写回工程。

## 8. 技术选型

| 层 | 选择 | 理由 |
|---|---|---|
| 语言 | C++20 | 性能、Qt 生态、跨平台 |
| UI | Qt 6 Widgets | 原生控件，KDE Breeze 主题自动跟随；纯原生 Qt 是既定决定 |
| 视口 | QOpenGLWidget + OpenGL 3.3 Core | Linux 稳定，shader 可直接改写 Mine-imator 参考实现 |
| 视频/音频编解码 | FFmpeg (libav*) | 解码与 H.264 编码一套库解决 |
| 音频输出 | Qt Multimedia QAudioSink | 跨平台，无额外依赖 |
| FFT | kissfft（单文件，BSD） | 频谱需求轻量，不引重型 DSP 库 |
| JSON | Qt JSON | 无额外依赖 |
| 构建 | CMake + Ninja + Clang | Ubuntu 26.04 默认工具链 |
| 测试 | Qt Test | 与 Qt 同栈，无额外依赖 |

## 9. 里程碑

| 阶段 | 内容 | 验收标准 |
|---|---|---|
| M0 骨架 | 仓库、CMake、主窗口、四面板布局、视口、时间轴、关键帧闭环 | 本机编译启动，导入图片并做出位移动画 |
| M0.2 工作台 | 视口浮层、图片/视频/文字/矢量四类实体、类型表单、视频首帧 | 每种类型都能从工作台创建并正确渲染 |
| M1 图层 | 时间轴行增删改排序、视口选择/拖拽/缩放/旋转、锚点、透明度、撤销 | 导入图片并手动摆位，撤销可用 |
| M2 动画 | 关键帧增删改、缓动、曲线编辑、播放控制、相机 | 图片做位移动画，实时预览流畅 |
| M3 媒体 | 视频解码与播放、音频导入/播放/波形、频谱对象 | 视频素材与音频同步播放 |
| M4 后处理 | FBO 链、bloom/色差/调色/颗粒/暗角/故障/扫描线、参数面板与关键帧 | 后处理参数可动画，预览正确 |
| M5 导出 | 离线渲染、H.264、音频混流、进度取消 | 导出 MP4 与预览一致 |
| M6 打磨 | 最近文件、快捷键、设置、预设、崩溃恢复 | 可日常使用 |

## 10. 从 Mine-imator 继承与舍弃

### 继承（重写，不直接复用代码）

- 场景列表 + 时间轴 + 关键帧 + 缓动的交互模型
- 相机后处理参数体系与 shader 思路（bloom、色差、色彩校正、颗粒）
- 相机震动模型（振幅/频率/衰减）
- 属性动画的数据组织方式（属性 → 轨道 → 关键帧）

### 舍弃

- GameMaker / GML / CppGen 转译层
- Qt 仅作窗口壳、UI 全自绘的模式
- Minecraft 方块、皮肤、模型、世界、资源包导入
- 3D 渲染、骨骼、光照、景深
- 156 个属性中与 3D/Minecraft 相关的部分

## 11. 非目标

- 不做通用视频剪辑器（无多轨剪辑、转场时间线、字幕轨）
- 不做 AE 替代品（无表达式、无 3D 图层、无插件生态）
- 不做协作/云端
- v1 只保证 Linux（Ubuntu 26.04 + KDE Wayland），Windows/macOS 在架构上留好口子但不承诺
