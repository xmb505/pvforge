# PVForge 修复指南（给 DeepSeek 的结构化施工单）

> **事实来源**：本指南基于 `docs/AI-HUMAN/人类确认过的施工图/`（#1~#11）及 `docs/human/` 人类原文，结合 2026-11-19 生产就绪审计（`#113`–`#145`）整理。

## 0. 全局红线（必读）

1. **禁止修改** `docs/human/` 与 `docs/AI-HUMAN/` 下的任何文件。
2. **文件体量红线**：任何新增或修改的源码文件不得超过 `1500` 行；每次提交前必须跑通：
   - `bash tools/check-code-size.sh`
   - `cmake --build build --target pvforge pvforge-tests -j$(nproc)`
   - `QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure`
3. **不保留兼容包袱**：没有外部用户，无需兼容旧工程或旧测试桩，死代码直接物理删除。
4. **已合并/关闭的 Issue**（勿重复开发）：`#84`（并入 `#126`）、`#88`（并入 `#115`）、`#103`/`#112`（并入 `#120`）、`#118`（非目标）、`#81`/`#141`（非目标）、`#64`（暂缓）。

---

## 1. 已拍板的语义边界（勿自由发挥）

- **#128（视频拆出音轨）**：导入带声视频时，自动拆出的音频实体设为该视频实体的**子实体**，并默认开启 `followParentRange = true`（父区间跟随）；这样移动/裁剪/删除视频时音轨自动同步，需要独立调整时可由用户关闭跟随或移出父级。
- **#135（私有时间线 相对/绝对）**：该开关**只改变私有时间线的刻度尺与区间显示**（相对模式显示 `0 ~ duration`，绝对模式显示全局帧号），播放指针与内部关键帧仍按统一工程帧计算，并保持缩放在各自私有浮窗内不被主轴重置。
- **#116（素材删除与 Undo）**：删除素材前先检查实体引用；确认删除时**不直接永久 `rm`**，而是将文件移入工程内 `.trash/<hash>/` 并纳入 Undo 栈，支持 `Ctrl+Z` 恢复。
- **#52（黑洞捕获大小）**：黑洞捕获默认**不修改实体缩放**；在黑洞参数中新增可选开关（默认关），开启后靠近事件视界时按距离缩放至 0。
- **#139（死代码处理）**：直接物理删除 `CheckpointManager`、`SettingsDialogLegacyPages.cpp` 及无用遗留管线与假测试，设置页文案对齐真实行为。

---

## 2. 分阶段修复顺序与验收

### Phase 1：规范与时间轴（最优先，紧接 UI 施工图）
1. **#133**：放开 `model` 验证器对子实体 `row < 0`（负轨道）的限制；根实体仍必须 `row >= 0`。
2. **#134**：移除 `EditActions::planMoveToRow` 中“拖到已占用行会交换行号”的旧槽位逻辑；在模型验证器中强制校验**同一父级下的同一轨道（`row`）上，任意两个片段时间区间 `[start, end)` 不得重叠**。
3. **#135**：打通私有时间线 `m_relativeTime` 对标尺和标签的渲染，移除硬编码 8 层嵌套限制（改为循环检测即可），停止每次刷新重置私有浮窗缩放/滚动位置。
4. **#136 + #23**：修复 `MainWindow` 选中同步清空 `m_entitySelection` 导致的框选/`Ctrl+单击` 多选失效；补齐时间轴右键「成组为文件夹」及批量移动/复制/删除；修复舞台 `Delete` 键、父级锁定传递、拾取顺序按图层排序机从顶到底。
5. **#137**：在 `TimelineAdapter::buildSnapshot` 接通音频 `peaks` 波形与 `trimStart`，恢复属性面板音频裁剪/淡入淡出控件。

### Phase 2：数据安全（P0）
1. **#113**：工程保存改为**事务化原子写入**（先写临时文件 + fsync/rename，跨根重排先落新后删旧或单目录事务），绝不留下重复 UUID 半写文件。
2. **#114**：在工程目录加 `QLockFile` 锁，双开同一 `.pvproj` 时给出明确提示并阻止互相覆盖。
3. **#115**：`ProjectDocument::undo()` / `redo()` 前强制 flush 待提交防抖编辑；`commitEntity(uuid, message)` 把真实 `message` 写入 Git commit。
4. **#116 + #117 + #142 + #144**：实现素材 `.trash/` 回收站式 Undo；修复 commit 失败误关窗口/误报崩溃恢复；`--open` CLI 与 GUI 统一走 Git 初始化与崩溃恢复流程；MCP `Tx::finish()` 失败时完整回滚并记 undo。

### Phase 3：成片导出与物理求解（P0）
1. **#127**：视频/HTML 实体使用模型自身逻辑尺寸，不得用解码代理像素尺寸（960px）冒充模型尺寸；保证预览与导出构图一致、2x/4x 导出不裁剪。
2. **#128**：修复导出混音必须按 `entity.end` 截断（写入 `destinationEnd`）；视频拆出音轨默认子实体+跟随父区间。
3. **#129**：导出时如遇缺失素材、视频解码失败、HTML 超时/未启动，必须**判定导出失败并报错**，禁止静默把占位卡烧进 MP4 后报成功。
4. **#126 + #52**：导出线程使用完全独立的只读快照与独立解码上下文，导出期间锁定 MCP/UI 写操作；补上黑洞捕获缩放开关（#52）。

### Phase 4：安全与并发（P0）
1. **#121**：`ProjectStoreGit` 强制隔离环境变量（`GIT_CONFIG_NOSYSTEM=1`、`GIT_CONFIG_GLOBAL=/dev/null`、`-c core.hooksPath=/dev/null` 等），禁止执行工程自带 `.git/hooks` 或 filter。
2. **#122**：HTML 图层通过 CDP `Fetch.enable` / 请求拦截阻止访问 `127.0.0.1` / `localhost` / `::1` 等 loopback 地址，禁止首帧后导航逃逸。
3. **#123 + #124 + #125**：MCP `asset.import` 工程外文件必须由 UI 弹窗让人类确认（不得仅靠 AI 传入 `confirm: true`）；加固路径逃逸与软链接检查；移除 MCP 批量工具中的 `processEvents()`，改用异步队列或安全锁避免关窗 UAF。

### Phase 5：性能、死代码与真实 CI（P1/P2）
1. **#139 + #140**：物理删除 `CheckpointManager`、`SettingsDialogLegacyPages.cpp` 及残留无用死代码；收拢剩余内部 `next` 命名。
2. **#130 + #131 + #132 + #143**：使用 `QElapsedTimer` 驱动播放时钟消除整数毫秒音画漂移；优化视频/图片解码缓存与 BT.601/709 色彩标记；消除 `childrenOf` $O(E^2)$ 深拷贝与无界缓存；播放混音改流式分块避免 UI 线程卡死。
3. **#120 + #138 + #119 + #56 + #59**：彻底修真 `.github/workflows/ci.yml`（全量跑 `ctest`，缺依赖返回 `77` 记 `Skipped` 而非 `Passed`，增加 Release 构建校验并去掉内嵌构建机绝对路径 `PVFORGE_MCP_BINARY`）。

---

## 3. 提交规范
每完成一个 Phase 或独立 Issue：
1. 跑通 `bash tools/check-code-size.sh` 与对应模块 `ctest`；
2. 提交信息注明关闭的 Issue 编号（如 `fix(model): allow negative rows in private timeline (#133)`）；
3. 完成后交由主审查官（Gemini）逐项 Review 验收。
