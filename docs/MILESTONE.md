# 里程碑 M1：可用的 2D PV 编辑器核心

记录时间：2026-11-07
仓库：`/home/xmb505/pvforge`
用途：压缩上下文后快速恢复项目状态。

## 一句话状态

用 Mine-imator 式工作流（对象树 + 时间轴 + 关键帧 + 相机式后处理）做 2D 音乐 PV 的编辑器核心已经可用：建项目、放图片/视频/文字/矢量、打关键帧、播放、撤销、保存/加载、素材打包。

**尚不能导出视频**，这是出片闭环的最后缺口。

## 已实现

### 工程

- 欢迎页：最近项目、新建项目、打开项目
- 新建项目对话框：名称/位置/分辨率/帧率/作者/描述
- `.pvproj` JSON 工程格式，相对路径，关闭未保存提示
- 标题栏显示 `[未保存]` / `[已保存]`
- 时间轴长度动态：取最长素材与最后关键帧，空工程为 0，播放头可无限前进

### 实体

- 图片、视频（首帧）、文字、矢量
- 文字：内容/字体/字号/颜色/对齐
- 矢量 9 种形状：矩形、圆角矩形、椭圆、直线、三角形、心形、星形、多边形、箭头
  - 参数：宽高、圆角、边数、内径比、填充、描边
- 每实体：位置、旋转、缩放、透明度、隐藏、锁定
- 自定义轴心（`rotPointX/Y`）：位置锚定轴心，改轴心自动补偿位置，旋转围绕轴心
- 锁定在数据层强制：值、关键帧不可改

### 动画与时间轴

- 逐属性关键帧轨道，线性插值
- 自动关键帧（默认开）
- 时间轴：固定缩放 + 横向滚动 + 自动跟随播放头
- `Ctrl+滚轮` 缩放（0.25–32 px/帧）
- 刻度行自适应：秒模式每秒按项目帧率均分为整数格（24 fps → 24/12/8/6/4… 格），帧模式按 1/5/10/20/50 帧分档并标帧号
- 刻度行可切换显示秒数或帧号（视图菜单，设置持久化）；状态栏同时显示帧与秒
- 点刻度行后 `←` / `→` 单帧步进；长按按工程帧率连续播放 / 倒退
- 刻度行拖播放头；轨道空白点击跳转；点关键帧选中不跳转
- 框选关键帧（行 × 帧范围），高亮选中
- 关键帧可以 `Delete` 删除，撤销恢复
- 时间轴左侧搜索框过滤对象
- 行内按钮（悬停）：隐藏、锁定、全选关键帧
- 双击对象名重命名

### 视口交互

- 选中框 + 四角缩放手柄
- Gizmo：X 右 / Y 上直角轴 + 旋转圆环
- 拖轴单轴移动，拖环旋转，拖本体自由移动
- 自定义轴心时：拖轴/白点移动轴心，关闭编辑后拖轴移动实体
- PPT 式角缩放：对角固定，位置补偿
- `Ctrl` 等比缩放；拖过中心镜像
- 吸附：画布中心线/边缘/四角、其他正交实体的边缘与四角
- 旋转吸附：默认 0°/90°，可开 180°/270°，容差 3°
- 拖动主轴约束（4:1）与青色对齐参考线
- 所有吸附可被 `Ctrl` 临时关闭

### 素材

- 工作台：媒体（图片/视频自动识别）、文字、矢量图标网格、内嵌素材库
- 素材库：`+` 多文件导入、搜索、预览、创建实体、删除（可选同时删实例）
- 异步导入：哈希/复制/解码在工作线程，状态栏进度条
- 内容寻址存储：`assets/<sha256>.<ext>`，天然去重，显示名保留
- 「全部打包到项目」开关（默认关），工程内素材存相对路径
- 视频时长参与时间轴长度

### 撤销 / 重做

- 覆盖：属性修改、实体创建/删除、隐藏锁定、重命名、关键帧删除、移动、缩放、轴心
- 连续拖动合并为一条：移动、缩放、轴心、单轴、旋转
- `Ctrl+Z` / `Ctrl+Shift+Z`

### 界面

- 右侧页签：项目属性（项目设置/环境设置/素材库）、实体属性页签（按需出现）
- 面板内容可折叠、可滚动
- 时间轴和视口配色跟随系统主题，图标优先用桌面图标主题
- 设置页：语言、吸附距离、边缘中点吸附、旋转吸附角、自动关键帧默认、新项目默认值、时间轴缩放、素材打包默认
- 语言文件 `assets/lang/zh_CN.json` / `en_US.json`（核心界面）

## 架构

```
src/
├─ core/     Project/Entity/Track/Keyframe、History+Commands、Settings、Lang、VectorShape
├─ anim/     （预留）
├─ render/   （预留，目前视口用 QPainter）
├─ media/    VideoDecoder（FFmpeg 首帧）、MediaImporter（哈希/复制/解码，可线程运行）
├─ audio/    （预留）
├─ export/   （预留）
├─ ui/       Viewport、TimelinePanel、PropertiesPanel、AssetLibraryPanel、
│            ProjectPropertiesPanel、EnvironmentPanel、WorkbenchPanel、
│            CollapsibleSection
└─ app/      MainWindow、WelcomeWindow、NewProjectDialog、SettingsDialog、AppIcon
```

关键设计：

- **位置 = 轴心**：变换 `T(pos) · R · T(-pivot) · S`，改轴心补偿位置，视觉不动
- **内容寻址素材**：哈希文件名 + 原始显示名
- **命令历史**：复合命令 + 合并，一次拖动一条撤销
- **异步导入**：重活在工作线程，主线程入库
- **求值/渲染未分离**：目前 Viewport 直接读 Project；导出前需要抽出快照（见下一步）

## 测试

每个测试是独立启动模式，`QT_QPA_PLATFORM=offscreen ./build/pvforge --xxx-test`：

self / save / delete / undo / rename / pack / lock / gizmo / pivot / pivot-gizmo / scale-handle / scale-undo / snap / settings / shape / import-async / drag-undo / rotation-snap / entity-snap / guide / timeline-frame / timeline-interaction / timeline-rowbuttons / open-timeline / timeline-zoom / play / workbench / layout / library / welcome

共 30 个模式，全部通过。

## 已知限制

- 没有视频导出（MP4）
- 视频只显示首帧，无逐帧播放
- 无音频、波形、频谱对象
- 关键帧只能线性插值，无缓动曲线
- 关键帧不能拖动改时间、复制、粘贴
- 旋转实体的角缩放仍围绕中心
- 实体单选（框选只作用于关键帧）
- 英文翻译只覆盖核心界面
- 没有多场景、多相机
- 3D 世界/Minecraft 资产完全移除（有意）

## 下一步：关键帧函数

优先级从高到低：

1. **缓动函数（easing）**
   - 关键帧增加 `easing` 字段：预设（线性/缓入/缓出/缓入缓出）+ 三次贝塞尔
   - `Track::evaluate` 用缓动插值替换线性
   - 关键帧右键菜单切换缓动；时间轴关键帧形状区分缓动类型
2. **曲线编辑器**
   - 下方可切换「图表编辑器」页，显示当前属性曲线
   - 拖控制点调整贝塞尔手柄
3. **关键帧编辑**
   - 拖动关键帧改时间（吸附帧）
   - 复制/粘贴/剪切、框选移动
   - 多选关键帧的属性面板显示
4. **关键帧函数（更远）**
   - 类似表达式：位置/旋转可绑定到时间、音频振幅、另一实体属性
   - 需要先抽出求值器（Project → FrameSnapshot），避免 UI 与渲染直接耦合
5. **导出 MP4**
   - 逐帧渲染到 FBO/QImage → FFmpeg 编码 → 音频混流
   - 复用同一求值路径，保证所见即所得

## 运行

```bash
cd /home/xmb505/pvforge
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/pvforge
```

依赖：Qt 6.10（Widgets/Concurrent）、FFmpeg 62、Clang 21、CMake 4。
