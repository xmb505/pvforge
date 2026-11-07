# PVForge

用 Mine-imator 的动画工作流做 2D 音乐 PV 的桌面合成器。

当前是 M0 最小版本：**导入图片 → 打关键帧 → 播放时动起来**。

## 构建

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/pvforge
```

自检（无窗口）：

```bash
QT_QPA_PLATFORM=offscreen ./build/pvforge --self-test
```

## 操作

| 操作 | 快捷键 |
|---|---|
| 导入图片 | Ctrl+I |
| 播放/暂停 | 空格 |
| 回到开头 / 跳到结尾 | Home / End |
| 视口适配窗口 | F |
| 视口缩放 | 滚轮 |
| 视口平移 | 中键拖动 |
| 移动图片 | 左键拖动 |
| 打关键帧 | 属性面板「在当前帧打关键帧」；默认自动关键帧，改数值即打帧 |

## 已实现

- 图片导入与视口显示
- 位置、旋转、缩放、透明度属性
- 逐属性关键帧与线性插值
- 时间轴刻度、关键帧标记、播放头拖动
- 30 fps 播放与循环
- 自动关键帧（默认开启）

## 路线

M0 关键帧闭环（当前）→ 保存/加载 → 缓动曲线 → 视频/音频 → 后处理 → MP4 导出。

完整设计见 [docs/DESIGN.md](docs/DESIGN.md)。
