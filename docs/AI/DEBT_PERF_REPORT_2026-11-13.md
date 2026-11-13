# AI 文档（非用户文档）

# 关键帧求值 / 工程校验 性能债：benchmark 成绩单

日期：2026-11-13
范围：`src/model/Keyframe.*`、`src/model/Project.*`、`src/next/app/ProjectDocument.*`、对应 tests、`bench/debt_perf_bench.cpp`
不含：UI、CMake、CI 改动（`bench/debt_perf_bench.cpp` 未加入任何 target，只能手工编译，见文末命令）

## 1. 结论

| 项目 | 优化前 | 优化后 | 提升 |
| --- | --- | --- | --- |
| `evaluateTrack`，100 关键帧，numeric | 3.37 us/次 | 0.27 us/次 | **12.5x** |
| `evaluateTrack`，100 关键帧，step | 3.15 us/次 | 0.076 us/次 | **41x** |
| `evaluateTrack`，1000 关键帧，numeric | 64.1 us/次 | 0.58 us/次 | **111x** |
| `evaluateTrack`，1000 关键帧，step | 64.2 us/次 | 0.36 us/次 | **176x** |
| `evaluateTrack`，1000 关键帧乱序，numeric | 79.8 us/次 | 6.83 us/次 | **11.7x** |
| `evaluateTrack`，1000 关键帧乱序，step | 81.9 us/次 | 6.59 us/次 | **12.4x** |
| `ProjectDocument::commitEntity`（改 1 个实体） | 0.176 ms/次 | 0.093 ms/次 | **1.9x** |
| `ProjectDocument::commitProject`（设置保存） | 0.122 ms/次 | 0.048 ms/次 | **2.5x** |
| `ProjectDocument::deleteEntities`（删 1 / 100） | 0.073 ms/次 | 0.030 ms/次 | **2.4x** |
| `model::validateProject`（无拷贝） | 0.099 ms/次 | 0.023 ms/次 | **4.3x** |
| 拷贝 + 删实体 + `validateProject` | 0.112 ms/次 | 0.040 ms/次 | **2.8x** |
| `validateProject`，100 层父子链 | 0.104 ms/次 | 0.038 ms/次 | **2.7x** |
| *对照*：`ProjectStore::saveEntity`（只写盘） | 0.066 ms/次 | 0.063 ms/次 | 1.0x（未改，噪声内） |
| *对照*：`validateEntity` × 100（未改） | 0.021 ms/次 | 0.020 ms/次 | 1.0x（未改，噪声内） |

两个对照组是刻意的：它们分别是「写盘地板」和「逐实体校验」，代码没动，用来证明差值来自被改的代码而不是机器抖动。

## 2. 测量条件

- 机器：Intel(R) Core(TM) Ultra 5 125H（18 线程），30 GB 内存，Linux 7.0.0-43-generic，Ubuntu 25.10。
- 编译器：g++ 15.2.0。
- 被测代码构建：CMake `RelWithDebInfo`（`-O2 -g`），静态库。
- benchmark 程序：`bench/debt_perf_bench.cpp`，自身以 `-O2` 编译，直接链接上面构建出来的 `pvf_model` / `pvf_store` / `pvf_next_core` / `pvf_next_document`。
- 优化前样本：`git worktree` HEAD（8b10391）单独构建的同类型库；优化后样本：当前工作区。两边用同一份 benchmark 源码、同一命令、同一台机器，交替各跑 3 次。
- 统计：每次 `measure()` 内部跑 3 个 trial，取**最好值**写进表格；表中数值是 3 次独立进程运行的**中位数**。
- 样本量：
  - `evaluateTrack`：每个 trial 10 000 次调用，查询帧来自预生成的 4 096 个随机值（整数与小数混合，穿插超出首尾的采样）；正式计时前先做 1 次预热调用。
  - `ProjectDocument`：100 实体 × 4 轨道（x 5 帧、y 3 帧、rotation 4 帧、opacity 2 帧）；`commitEntity` 每 trial 500 次、`commitProject` 100 次；`deleteEntities` 每 trial 清空 100 个实体、共 3 个新建工程；`validateProject` / 拷贝对照 / `saveEntity` 各 100 次；`validateEntity` 对照、100 层链对照、空工程对照各 25 次。
  - `pvf_model_tests`、`pvf_next_document_tests` 等见第 6 节。

## 3. 改了什么

### 3.1 `evaluateTrack`：不再复制 + 排序整条轨道

旧实现每次调用 `QVector<Keyframe> sorted = track.keys;` 再 `std::stable_sort`，然后再线性扫一遍。
`Keyframe` 带 `QString note` 和 `QJsonObject extras`，复制/移动会做原子引用计数，排序还要 `malloc` 临时缓冲；这就是 1000 帧 64 us 的来源。

新实现（`src/model/Keyframe.cpp`）：

1. 先 `isFrameOrdered()` 单趟扫描（只比较相邻 `frame`）。编辑产生的轨道天然有序，命中后**不分配、不复制、不排序**。
2. 命中有序：二分查找第一个 `frame > lookup` 的位置，取左右邻居插值。
3. 未命中（乱序轨道）：`thread_local QVector<int>` 里只对**下标**做 `stable_sort`，再按下标访问原 `keys`。仍然不复制/不移动 `Keyframe`，缓冲跨调用复用。

插值与缓动语义逐条对齐旧实现（见 3.4 的等价性论证，以及 3.4.1 的 300 万次差分对拍）。

### 3.2 `validateProject`：去掉每实体一个 `QSet`，去掉循环里的 UUID 哈希

旧实现的父子环检测对每个实体新建一个 `QSet<Uuid> path` 并入 `resolved`；`Uuid` 是 36 字符 `QString`，一次 `QHash` 操作 ~100 ns。100 实体时这一段单独占 58 us（用探针程序分段测得，`validateProject` 总 82 us）。

新实现（`src/model/Project.cpp`，`validateProjectCore`）：

1. 实体校验那一趟顺手收集 `QVector<const Entity*>`，后面全部按下标访问。
2. 父范围检查那趟本就 `constFind(parentUuid)` 一次，把结果转成父下标（`QHash<const Entity*, int>`，指针哈希）。
3. 环检测改成一次 `QVector<char> walkState` 三色 DFS，每个节点只走一次；不再有字符串哈希、不再有每实体容器分配。
4. `validateSolverTargets` 的 `label` 字符串（`QStringLiteral(...).arg(...)`）从「每个 solver 段都构造」改成「只在报错时构造」。正常工程一分钱不付。

### 3.3 `ProjectDocument`：删实体不再深拷贝整个工程

旧 `validateForWrite` 一开始就 `model::Project check = project;`，删实体时对 `check.entities` 做 `remove()`，触发 `QHash` 写时复制（把 100 个 `Entity` 全部拷一遍），然后校验整个副本。

新实现：

- upsert 路径：直接在真实工程上校验（本来就没有拷贝必要）。
- delete 路径：新增 `model::validateProjectWithout(project, removed, error)`——语义是「把这些实体当作已经被删掉」，实体自身不校验，父引用和 solver 目标引用把它们视为不存在。`ProjectDocument` 只收集 `QSet<Uuid> removed` 再调它。
- 仍然保留「先存在性、再锁定、最后整体校验」的顺序和错误文案。

### 3.4 等价性论证（为什么这不是「行为变了」）

- **边界帧**：`lookup < 第一帧`→第一帧值；`lookup >= 最后一帧`→最后一帧值；正好命中某帧→该帧值（不套缓动）；区间内→`left + (right-left) * easedProgress(left.easing, t)`。与旧代码逐行对照。
- **重复帧**：旧代码稳定排序后线性扫，取「最后一个 `frame <= lookup`」，即重复帧里的最后一个。新代码下标 `stable_sort` 后二分，`low-1` 同样是最后一个重复帧。语义一致（`model_tests` 的 unsorted 用例覆盖）。
- **乱序轨道**：仍然支持（`track-evaluation` 测试里的 10/0 反序轨道），只是排序对象从 `Keyframe` 变成下标。
- **非有限帧**：NaN 查询旧代码落到「命不中任何帧」的分支并从第 0、1 个关键帧插值；新代码 `low == 0` 返回第 0 个关键帧值。旧代码在该情形下对单关键帧轨道还会越界读（UB），新代码不越界。实际上帧号来自整数/有限采样，任何生产调用点都不会传 NaN；这是唯一一处数值差异，且是从 UB 收敛成安全值。
- **删除校验**：`check.entities.remove(uuid)` 后的副本等价于「实体不存在」。新实现用 `removed` 集合表达同一件事：自己跳过、父引用视为缺失、solver 目标视为缺失、索引键不再比对。`model_tests` 新增 `project-validation-without` 逐条比对。
- **删除批量锁**：旧代码对副本逐个删、逐个查锁。看似「先删掉的祖先不会被锁检查走到」，但任何被锁实体的后代自身也会被锁拦下（它的祖先链必经该锁实体），所以两种实现接受/拒绝的集合相同。`deleteChildOfLockedParentIsRejected` 继续通过。
- **重复 UUID 删除**：旧代码第二次 `constFind` 落空 → 「实体不存在」；新代码用 `removed.contains(uuid)` 复现同一文案（新增用例 `duplicateDeleteIsRejected`）。
- **upsert 存在性 / 重复 upsert**：与旧代码一致（重复 upsert 仍被接受，因为没有删除动作）。
- **撤销/恢复**：`ProjectStore` 的 undo/redo 栈存的是**文件字节快照**（`UndoEntry{before, after}`），与校验无关；`ProjectDocument::undo/redo` 只动计时器和内存栈。这次改动没有触碰它们，`undoRedoUseMemoryHistory`、`undoDropsPendingDebounce` 等用例通过。

#### 3.4.1 差分验证（不只是"我觉得一样"）

两段一次性差分程序（放在 `/tmp/opencode/`，不入库）：

1. **`evaluateTrack` 新旧对比**：把 HEAD 的求值函数原样复制成 `oldEvaluateTrack`，用随机轨道（0～12 个关键帧、帧号 0～15 允许重复、7 种缓动含未知名字、随机反序）× 查询帧（整数、±0.5、±0.25、超出首尾、±1e9）× step/非 step 对拍**位相等**：

   ```
   compared=3000000 mismatches=0
   ```

   覆盖重复帧、乱序轨道、单关键帧轨道（旧实现在这里的单关键帧 + NaN 会越界读，新实现不越界）。

2. **`validateProject` / `validateProjectWithout` 新旧对比**：生成 4000 个随机工程（父子树、环、悬空父、坏范围、solver 目标指向不存在/自己/空、重复索引键、marker、坏 settings），分别链接 HEAD 的 `pvf_model` 与优化后的 `pvf_model`，各自 dump「是否通过 + 错误文案」，`QT_HASH_SEED=0` 固定哈希序：

   ```
   diff val_base.txt val_new.txt   →   只有新增的 removal-mismatches 行不同
   （4000 个工程中 461 个通过；接受/拒绝与错误文案逐字相同）
   removal-mismatches=0            →   validateProjectWithout 与「拷贝后删实体再校验」逐例一致
   ```


### 3.5 为什么**没有**做「只校验被改实体」的增量校验

先测了成本结构（100 实体，优化后）：`commitEntity` 93 us = `saveEntity` 写盘 61 us + `validateProject` 23 us + 计时器/信号 ~9 us。

也就是说整工程校验现在只占一次编辑的 25%，写盘占了 66%。就算把校验砍到 0，这条路径最多再快 1.4x；而增量校验要在两处维护同一套规则（父范围、子范围、solver 正向/反向引用、锁定祖先），规则漂移的代价远大于 23 us。结论：先不做，等写盘路径（JSON + `QSaveFile`）也优化过之后再看，那时校验才会重新成为主要成本。

## 4. 复现命令

```bash
# 1) 构建被测静态库（与本次测量一致）
cmake -S . -B build-debt-perf -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=ON
cmake --build build-debt-perf -j18 --target pvf_next_document

# 2) 编译 benchmark（不在 CMake 里，故意如此）
g++ -std=c++20 -O2 -fPIC bench/debt_perf_bench.cpp -I src \
  $(pkg-config --cflags Qt6Core Qt6Gui) \
  build-debt-perf/src/next/app/libpvf_next_document.a \
  build-debt-perf/src/next/core/libpvf_next_core.a \
  build-debt-perf/src/store/libpvf_store.a \
  build-debt-perf/src/assets/libpvf_assets.a \
  build-debt-perf/src/model/libpvf_model.a \
  $(pkg-config --libs Qt6Core Qt6Gui) \
  $(pkg-config --libs libavformat libavcodec libavutil libswscale libswresample) \
  -o /tmp/opencode/debt_perf_bench

# 3) 与优化前对比：另开一个 HEAD worktree，构建同名库，用同一份 bench 源码链接
git worktree add --detach /tmp/opencode/pvforge-baseline HEAD
(cd /tmp/opencode/pvforge-baseline && cmake -S . -B build-base \
   -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=OFF \
 && cmake --build build-base -j18 --target pvf_next_document)

# 4) 跑
/tmp/opencode/debt_perf_bench all 1        # 优化后
# 优化前：同一编译命令，把 build-debt-perf 换成 build-base，输出到另一个二进制
```

`bench` 的第一个参数 `model|document|all` 选段落，第二个参数是倍率（`1` = 表格里的样本量）。

## 5. 已知取舍

- `evaluateTrack` 有序快路径仍是 O(n) 的「是否有序」扫描（1000 帧 ≈ 0.36 us）。要做到 O(log n) 必须把「有序」变成写入时不变量，而 `Track::keys` 是公开的 `QVector`，编辑侧（时间轴、MCP、旧 UI）都在直接 `append/insert`，无法在不改所有调用点的前提下维持缓存。0.36 us/帧/轨道已远低于一帧预算。
- 乱序轨道 6.6 us 仍比有序慢一个量级（要排序 + 复用缓冲），但乱序只是「编到一半」的中间态，而且比旧实现快 12x。
- `bench/debt_perf_bench.cpp` 没有进任何 CMake target（本次任务禁止改 CMake），需要按上面命令手工编译。

## 6. 测试结果

两套构建都跑了同一批用例：

- `build-debt-perf`：`RelWithDebInfo`（本报告的性能数据就来自这里），带 `PVFORGE_BUILD_SELF_TESTS=ON`。
- `/tmp/opencode/pvforge-verify`：从 HEAD（8b10391）另开 worktree，只把本 diff 的 7 个文件拷进去，`Debug` 干净复核构建（排除工作区里其他 agent 的未完成改动干扰）。

| 用例 | RelWithDebInfo | Debug 干净复核 |
| --- | --- | --- |
| `pvf_model_tests` | 1454 checks / 0 failures | 1454 checks / 0 failures |
| `pvf_next_document_tests` | 18 passed / 0 failed | 18 passed / 0 failed |
| `pvf_anim_tests` | 113 checks / 0 failure | 113 checks / 0 failure |
| `pvf_store_tests` | 41 passed / 0 failed | 41 passed / 0 failed |
| `pvf_export2_tests` | 286 checks / 0 failures | 286 checks / 0 failures |
| `pvf_workbench_services_tests` | 23 passed / 0 failed | 23 passed / 0 failed |
| `pvforge-tests --solver-playback-perf-test` | OK，7.08 ms/帧 = 141 fps | OK，8.19 ms/帧 = 122 fps |

新增用例：

- `model_tests`：`track-evaluation-duplicates`（重复帧在首/中/尾 + 反序 + 单关键帧）、`project-validation-without`（removal 集合逐条语义）。
- `ProjectDocumentTests`：`duplicateDeleteIsRejected`、`deleteBatchRejectsDanglingChild`。

额外广扫：`ctest -R "next\.|pvf_model_tests|pvf_anim_tests|store\.|export2\."` 共 69 个用例，**69/69 通过**（含 `next.history`、`next.mcp_log`、`next.media_services`、`next.playback_controller`）。

`tools/check-code-size.sh`：通过，没有文件超 2500 行、没有抬高 baseline。
