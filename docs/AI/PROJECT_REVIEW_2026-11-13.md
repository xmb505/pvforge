# PVForge 项目审查（2026-11-13）

> 审查对象：`/home/xmb505/pvforge`，HEAD `6eb80a0`（merge: delete the reference shell (#77)）。
> 审查方式：独立读代码、跑局部 ctest（`model/store/solver` 子集，32 项全过）、跑 `pvforge-tests --self-test` 与 `dist/pvforge`。**没有**把 `docs/AI/` 里的推导/“已完成/已清理”结论当依据。
> 本文件只记录发现，不改代码、不提交。
>
> 局限：审查期间 HEAD 从 `273d9a0` 前进到 `6eb80a0`（删参考壳），本报告的代码/行号以 `6eb80a0` 为准；没有跑全量 CTest（含 Chrome 用例耗时且依赖环境），因此“测试全绿”类结论不在此报告范围内。

## 一句话结论

这不是烂代码，是**装修过度的毛坯房**：模型、Store、MCP、缓存的核心工程质量不差；但交付壳长期叠着两套 UI、两套测试、两套构建体系，文档承诺了大量已经删掉的功能，验收流程（CI / dist 验收命令）是纸面的。项目最大的问题不是写错，而是**没人敢删、也没人真验**。

按代码量粗算：

| 分类 | 行数 |
|---|---:|
| `src/**/*.{cpp,h}` 总计 | 175,193 |
| 产品代码（去掉 `src/next` 与所有 `*/tests/`） | 99,881 |
| 测试代码（所有 `*/tests/`） | 56,452 |
| `src/next/`（所谓“参考壳”残部 + 交付服务） | 28,520 |
| `src/core/`（已“退休”的旧模型） | 9,367 |
| `docs/AI/*.md` | 13,527 |
| `docs/human/`（人类规范） | **125** |

7 天 421 次提交，2026-11-13 09:11 一个提交删掉 13,082 行（`git show --shortstat 6eb80a0`）。这不是迭代，是抽搐。

---

## P0：用户会被直接骗到的三件事

### P0-1 交付版没有「图表」页和「波形」页，但三份用户文档还在教用户用

文档承诺：

- `README.md:25`：「图表页曲线、波形页」，并注明依据 `src/ui/CurveEditor.cpp`
- `README.md:58`：「「图表」页调曲线，「波形」页对音频」
- `docs/USAGE.md:32-35`、`:83`
- `docs/FEATURES.md:12-13`、`:61`、`:119`

代码事实：

- `src/ui/TimelinePanel.h:80` 只剩 `enum class EditMode { Clips, Keyframes }`，没有页签。
- `src/ui/CurveEditor.cpp`、`src/ui/WaveformView.cpp` 只在 `CMakeLists.txt:153-155` 的 `if(PVFORGE_LEGACY_SHELL)` 里，Release 根本不编译（`-DPVFORGE_LEGACY_SHELL=OFF`）。
- 连测试都自己写明了：`src/tests/EditorTests.cpp:276-280`「curve editor lives in the retired timeline 「图表」 page … `--curve-editor-test` was retired with issue #72」；`:412-416`「The 「波形」页 … is gone」。
- 同理场景/注释带：`src/tests/SceneTests.cpp:4-10`「scenes are not part of the next model … retired with issue #72」。

后果：README 里写的操作路径，用户点不出来。更糟的是 README 把 `src/ui/CurveEditor.cpp` 列为“依据”，而这个文件不在交付构建里——这是**引用一个不存在的交付物当证据**。

这不是“文档滞后”，是产品功能被单方面删除（`docs/AI/OLD_UI_FEATURES.md:11` 里 AI 自己决定“曲线图编辑器和语言包…不进入交付范围”），然后用户文档没改。`README.md:23` 和仓库约定都要求旧 UI 的布局/面板/交互保留；这里删的是面板本身。

### P0-2 `dist/` 里的“验收命令（本机已通过）”对交付二进制是假的

`dist/RUN.txt:53-63` 写着：

```
验收命令（本机已通过）
  QT_QPA_PLATFORM=offscreen ./pvforge --self-test
  ... --session-test / --viewport-test / --export-test ...
```

事实：

- `src/main.cpp:63-69` 的自检分发整段在 `#ifdef PVFORGE_SELF_TESTS` 里；Release 构建 `PVFORGE_BUILD_SELF_TESTS=OFF`（`CMakeLists.txt:199`）。
- `strings dist/pvforge | grep -- --self-test` → **0 条**。
- 实测 `timeout 5 ./dist/pvforge --self-test` → **exit 124（挂住直到超时）**，因为它忽略未知参数直接进欢迎页。
- `dist/RUN.txt:1` 还写「PVForge 0.2.0」，而二进制是 0.3.0。

也就是说：这包发布物**没有任何一条验收命令可以执行**，所谓“本机已通过”只可能是在 `pvforge-tests` 上跑的，不是在这个包上跑的。发版流程是编的。

### P0-3 CI 只数行数；README 却说 CI 在跑 Qt 6.6.3/6.7.3 × Debug/Release

- `.github/workflows/ci.yml:3` 注释：「目前只保留代码量护栏。构建、测试、Release 仍由本地手动执行」。
- `ci.yml:19-26` 唯一 job：`bash tools/check-code-size.sh`。
- `README.md:98`：「GitHub Actions 用同一套 preset 跑 Qt 6.6.3/6.7.3 × Debug/Release」。

两句话互相打脸。实际效果：

- 没有任何自动构建 → 删掉/漏掉一个源文件，CI 不会红。
- 没有任何自动测试 → 323~350 个 ctest 用例、100 个自检开关全是本机人肉。
- 唯一的门禁只防“单文件变大”：把 5,773 行的 `MainWindow.cpp` 拆成三个 1,924 行文件就算“通过”（总复杂度没变）。`tools/check-code-size.sh:120` 只检查 `n > FILE_LIMIT`，`baseline` 把 5,773 行冻结成合法上限（`tools/code-size-baseline.txt`）。删掉 13k 行的参考壳，它也只回一句“可收紧 0 项”。

---

## P1：结构性腐烂

### P1-4 交付构建里编着一整套没人链接的第二 UI

`CMakeLists.txt:259` **无条件** `add_subdirectory(src/next/properties)`，`:258` 无条件加 `src/next/timeline`。于是默认 `cmake --build` 会编译：

| 目标 | 内容 | 行数 | 交付链接？ |
|---|---|---:|---|
| `pvf_next_properties` | `src/next/properties/PropertiesPanel.cpp` 等 | 3,853+ | 否（`build-release/CMakeFiles/pvforge.dir/link.txt` 无此项） |
| `pvf_next_timeline` | `src/next/timeline/TimelineWidget.cpp` 等 | 3,293 | 只被 `pvf_next_properties` 链接 |
| `pvf_next_assets` | `src/next/assets/AssetPanel.cpp` | 1,556 | 被 `pvf_next_services` 链接，但交付代码 0 处 include |

证据：

- `src/next/properties/CMakeLists.txt:37` 建库，`:52-61` 链接，`:59` 甚至把 `Qt6::Test` 以 `PUBLIC` 链进非测试库。
- `src/next/timeline/CMakeLists.txt:27`、`:58-67`，同样 `Qt6::Test PUBLIC`。
- 两份 `PropertiesPanel` 有 11 个同名方法（`buildUi`、`refresh`、`buildSolverSection`、`rebuildSolverCards`…），一份 5,580 行、一份 3,853 行。
- 三个文件被**重复编译两次**：`CMakeLists.txt:111-116` 把 `src/next/properties/{SolverPreviewWidget,SpecialContentEditor,TextLayoutDialog}.cpp` 塞进 `pvf_app`，同时 `pvf_next_properties` 又编一遍。

后果：默认构建多编约 8,700 行死代码；改 A 面板不会影响 B 面板，但两个面板会持续漂移；代码体量统计被死代码稀释；“删了参考壳”这个提交只删了窗口，没删这些面板。

### P1-5 两套 UI 用 `#if` 拼在同一批文件里，Release 编掉 7,705 行旧 UI

- 全仓库 **158 处** `#if PVFORGE_LEGACY_SHELL`（`src/ui/TimelinePanel.cpp` 27 处、`src/app/MainWindow.cpp` 25 处、`src/ui/PropertiesPanel.cpp` 16 处…）。
- 按块统计，Release 里被预处理掉的旧 UI 代码约 **7,705 行**：`MainWindow.cpp` 3,158、`Viewport.cpp` 1,671、`PropertiesPanel.cpp` 776、`AssetLibraryPanel.cpp` 719…
- 同一个类两个构造函数：`src/app/MainWindow.cpp:349`（`MainWindow(Project*)`，旧）与 `:769`（`MainWindow(EditorSession*)`，新）。
- 同一个 `paintEvent` 两条渲染路径：`src/ui/Viewport.cpp:3539`（旧，`#if` 包住）与 `:2366` 的 `paintSession()`。

后果：任何 UI 改动都要同时面对两套状态；`README.md:23` 说“沿用旧 UI，依据 `src/app/`、`src/ui/`”，但仅 `src/ui/` 就有 4,185 行旧逻辑在交付版被预处理掉（`src/ui` 总计 29,474 行，约 14%）。“保留旧 UI”变成了“保留旧 UI 的源码”。

### P1-6 生产头文件里留着一排假接口，注释还说“给自检用”

`src/ui/TimelinePanel.h:161-176`：

```cpp
// Scenes and annotations are not drawn by the clip timeline yet. The
// accessors stay so the existing UI self-tests keep compiling.
QString selectedSceneForTest() const { return QString(); }
QRect sceneBandRectForTest(const QString& id) const { Q_UNUSED(id); return QRect(); }
int sceneBandCenterYForTest() const { return 0; }
bool sceneActiveForTest(const QString& id) const { Q_UNUSED(id); return false; }
```

实测这些函数在 `src/tests/` 里**调用次数为 0**（对应测试早被 issue #72 退休）。注释是假的，接口是死的，返回值是常量。这类“为了让测试编译而返回假值”的桩，比没有接口更危险：以后谁补测试，会拿到 `QRect()` 而不是失败。

### P1-7 关键帧求值每次都复制 + 全量排序（每属性、每帧）

`src/model/Keyframe.cpp:29-33`：

```cpp
QVector<Keyframe> sorted = track.keys;          // 整条轨道深拷贝
std::stable_sort(sorted.begin(), sorted.end(),  // O(n log n)
                 [](const Keyframe& a, const Keyframe& b) { return a.frame < b.frame; });
```

调用点在热路径上：

- `src/ui/Viewport.cpp:336`、`:470`（每实体、每属性的 paint）
- `src/anim/Anim.cpp:62`（动画上下文求值）
- `src/audio2/AudioMix.cpp:65`、`:93`（音频混音，逐帧）
- `src/export2/Export.cpp:105`、`:135`（导出逐帧）
- `src/ui/PropertiesPanel.cpp:711`、`:2538`、`:2550`

关键帧确实没有维持有序：解析时直接 `keys.append`（`src/model/Keyframe.cpp:189-206`），编辑时也是追加（`src/ui/Viewport.cpp:381-400`、`src/ui/PropertiesPanel.cpp:683`），所以“排序”这一步本身是必要的——但正确做法是**每次插入时插到有序位置**，或在加载时排一次，而不是每次求值都复制整条轨道再排。60fps 播放、拖动、导出各乘以“实体数 × 属性数”。这是**架构级热路径缺陷**，不是微优化问题。

### P1-8 每次编辑深拷贝整个工程做校验

`src/next/app/ProjectDocument.cpp:44-49`：

```cpp
model::Project check = project;   // 深拷贝所有实体、轨道、关键帧
```

调用点：`commitEntity`(`:185`)、`commitEntities`(`:200`)、`deleteEntities`(`:218`)、`commitChanges`(`:240`)、`commitProject`(`:264`)。也就是说拖动一次 gizmo、打一个关键帧，都要先把整个工程复制一遍，再把所有实体 `validateProject`（`src/model/Project.cpp:242`）一遍。大工程的手感会直接塌掉。

### P1-9 “原子保存”只对单文件成立，跨文件失败不回滚

- `ProjectStore::saveProject`（`src/store/ProjectStore.cpp:1127-1141`）只写 `project.json`；实体由 `saveEntity`（`:1103`）逐个写。
- `ProjectDocument::commitChanges`（`src/next/app/ProjectDocument.cpp:232-257`）先循环 `saveEntity`，再循环 `deleteEntity`；中途失败直接 `return false`，已写/已删的不会恢复，内存里的 `m_project` 也已经被改过。
- undo 快照恢复同样是逐文件写（`src/store/ProjectStore.cpp:1752-1775`），应用到一半失败会留下混合状态。

`README.md:33` 的“原子保存”因此只在单文件粒度成立。断电/磁盘满/权限错误时，工程会处于“一半新一半旧”的状态。单文件用了 `QSaveFile`（好），但整体没有事务/日志。

### P1-10 语言开关是装饰品

- 设置里能选 English：`src/app/SettingsDialog.cpp:904-909`。
- 语言包总共 **49 个 key**（`assets/lang/{zh_CN,en_US}.json`）。
- 产品代码里硬编码中文 `QStringLiteral("…")` **3,108 处**，全仓库 4,587 处；`Lang::t(...)` 只有 77 处，`tr("...")` 135 处。
- 全仓库没有 `.ts`/`.qm`，CMake 里没有 `qt_add_translations`。

切换 English 后约 1% 的界面会变。这比没有语言选项更糟：用户会以为程序坏了。

### P1-11 测试体系分裂：ctest 跑的和自检跑的不是一回事

- `src/tests/` 有 **100 个** `--*-test` 开关（`src/tests/TestRegistry.cpp:32-48` 注册 12 组），**没有任何一个**进入 CTest。
- CTest 的 300+ 用例全在模块子目录里；`ctest -N` 里搜不到 `--self-test`（实测 `grep self-test` 在 `CTestTestfile.cmake` 中为 0）。
- `src/tests/` 里有 11 个文件、合计 **17,570 行**直接构造/操作 `core::Project`（例：`src/tests/EditorTests.cpp:284` `new pvf::Project`，`:297` `MainWindow window(project)`），而 `core::Project` 在 Release 里根本不编。`CMakeLists.txt:49-57` 甚至为了这些测试强制 `PVFORGE_LEGACY_SHELL=ON`。
- 结果：`ctest` 全绿 ≠ 交付壳被测过；真正的应用级回归要靠人肉记住 100 个开关名。

再加上 `src/tests/SolverTests.cpp` 与 `src/solver/tests/SolverTests.cpp` 同名、`src/tests/` 与 `src/next/*/tests/` 重叠，测试资产本身就是一笔糊涂账。

### P1-12 175k 行 C++，零编译警告配置

全仓库 `grep` 不到 `-Wall`、`-Wextra`、`-Werror`、`add_compile_options`、`target_compile_options`、sanitizer。编译器默认警告等于关着眼睛写。对一个每周新增两万行、刚删一万三千行的仓库，这是最便宜也最该先补的护栏。

---

## P2：够不上事故，但每条都在给未来加利息

1. **构建系统被当成依赖注入框架**：25 个子目录各自 `project()` 自称独立工程；52 处 `add_subdirectory(... ${CMAKE_CURRENT_BINARY_DIR}/xxx_dep)` 用 `if(NOT TARGET ...)` 去重。这意味着同一个模块从根构建和从子目录构建会得到两套不同的配置，CMake 的 target 图已经不是图，是补丁摞补丁。
2. **20GB 构建垃圾**：仓库根下 20 个 `build-*` 目录，合计 20GB。两个测试构建目录的 CTest 清单互相矛盾：`build-debt-tests` 报 323 项，`build-coord-tests` 报 350 项（里面还有已删除的 `next.camera_monitor`）——说明没有一个目录跟得上 HEAD。`build-release/` 里还留着 11 月 10 日的 `pvforge-next` 二进制和 `libpvf_next_app.a`，而这两样当前 CMake 已不再产生。
3. **272 个 `*ForTest` 成员写在生产头文件里**（`grep -rn ForTest --include=*.h src | grep -v /tests/ | wc -l`），例如 `src/app/MainWindow.h:115-146`、`src/app/ExportDialog.h:47-64`。没有 `#ifdef` 隔离，发布 ABI 里塞满测试后门；测试也因此永远只敢测内部实现。
4. **`Qt6::Test` 被 `PUBLIC` 链进两个非测试库**：`src/next/properties/CMakeLists.txt:59`、`src/next/timeline/CMakeLists.txt:65`。任何链接它们的程序都会顺带依赖 Qt Test。
5. **提交信息参数是死的**：`ProjectDocument::commitEntity/commitEntities/commitChanges/commitProject` 全部 `Q_UNUSED(message)`（`src/next/app/ProjectDocument.cpp:182/197/215/237/261`），真正写 git 的永远是常量 `kHumanCommitMessage`（`:19`、`:281`）。API 让调用者以为能写提交信息，实际被丢掉。
6. **MCP 批处理在事务中间泵事件循环**：`src/app/EditorSession.cpp:163-165` 的 batch checkpoint 直接 `QCoreApplication::processEvents()`，没有 `ExcludeUserInputEvents`（对比导出路径 `src/app/MainWindow.cpp:3567` 用的是 `ExcludeUserInputEvents`）。设计上用 `BusyFlag`（`src/mcp2/HttpServer.cpp:228`）挡住了第二个 `tools/call`，但挡不住重绘/定时器在“写了一半”的工程上跑。目前靠 `humanWritesEnabled` 兜底，属于“能工作但踩钢丝”。
7. **重复的枚举→文案映射**：`src/ui/PropertiesPanel.cpp:58-90` 的 `typeName` 与 `:100-126` 的 `contentKindLabel` 是同一套实体类型的两份中文表，`src/next/properties/PropertiesPanel.cpp:259` 还有第三份。加一种实体要改三处，漏一处就显示空白。
8. **文档反客为主**：`docs/AI` 31 个文件 13,527 行，`docs/human` 125 行且 `module-pseudocode/` 只有 README、没有任何模块伪代码。人类规则说“动某模块前先读这里对应的 `.md`”——实际无文件可读，于是 AI 只能读自己的推导，再把自己的推导当需求。这就是漂移的根。
9. **提交节奏**：421 commits / 7 天，`src/main.cpp` 被改 182 次，`README.md` 93 次，`MainWindow.cpp` 89 次。`main.cpp` 现在只有 128 行，却被反复重写——说明每次都在改入口去适配测试/壳，而不是把变化收敛在模块里。
10. **工程里没有 `.clang-format` / `.editorconfig` / `.clang-tidy`**，但有一个叫 `build-debt-format` 的构建目录，说明“格式债”被讨论过，然后没有被解决。

---

## 确实做对的地方（免得这份报告被当成情绪）

- **模型层干净**：`pvf_model` 只链 `Qt6::Core`，没有 Widgets 泄漏（`src/model/CMakeLists.txt`）；`validateProject` 正经验证父链、循环、时间范围、solver 目标环（`src/model/Project.cpp:242-350`）。
- **Store 的 git 语义想清楚了**：`--only` 提交、`restoreCommit` 的脏文本保护、撤销快照、迁移链（`src/store/ProjectStore.cpp:497-780`、`:1127-1290`）。
- **MCP 安全做得比大多数桌面 AI 集成好**：只绑 `127.0.0.1`、校验 `Host`/`Origin`、Bearer token、配置文件 0600（`src/mcp2/HttpServer.cpp:73-144`、`:522-580`）、急停语义明确（`HttpServer.h:68-71`）。
- **缓存复用了同一个 LRU 内核**：`framecache::ImageLru` 同时被 `workbench::Cache` 和 `htmlcache::MemoryCache` 使用（`src/framecache/ImageLru.h`），没有各写一套。
- **单文件写入用了 `QSaveFile`**，没有出现 `fopen`+`rename` 手搓。
- 自检和单元测试数量可观，说明作者确实在验证；问题是**验证的位置和对象错了**，不是不测。

---

## 建议的修复顺序（按性价比）

1. **先让文档说实话**：删掉 README/USAGE/FEATURES 里“图表页/波形页/曲线”的承诺，或者把页面加回来。同时修 `dist/RUN.txt`（版本号、可执行命令）。这是 30 分钟的事，但现在它每天在骗人。
2. **CI 加最小构建 + ctest**：一个 Debug job 就够，先别管矩阵。`ci.yml` 现在连“能不能编过”都不检查。
3. **把 `src/tests/` 的 100 个开关接进 CTest**，或者至少加一个 `--all` 聚合入口和一个 `add_test`。否则 25k 行测试等于没有。
4. **删掉不产生交付行为的库**：`pvf_next_properties`、`pvf_next_timeline` 根本不进交付链接，`pvf_next_assets`（`AssetPanel`）进了链接但 0 处引用。把 `add_subdirectory` 改成按需/`EXCLUDE_FROM_ALL`，或者直接删。它们不交付、不测试交付路径，只贡献编译时间。
5. **修 `evaluateTrack`**：假设轨道已排序，二分查找；排序只在写入时做。这是一天能拿到的最大性能收益，且风险低（有测试覆盖）。
6. **`validateForWrite` 改成只校验被改动的实体 + 受影响父链**，别深拷贝整个工程。
7. **给 CMake 加 `-Wall -Wextra`（先不加 `-Werror`）**，让编译器开始说话。
8. **拆分 `MainWindow` 的旧/新构造函数**，用基类/组合而不是 `#if` + 两个构造函数。这是 `MainWindow.cpp` 5,773 行的根因。
9. **把 `*ForTest` 赶到独立的 `#ifdef PVFORGE_SELF_TESTS` 区**，或者用 friend test fixture。
10. **决定 `core::Project` 的生死**：要么迁移剩余自检到 session 模型后删除（9,367 行），要么明确它还在服役。现在它既“退休”又被测试强制编译，最贵的状态。

---

## 附：怎么复核这份报告

```bash
# 交付版没有曲线/波形
grep -n "图表\|波形" README.md docs/USAGE.md docs/FEATURES.md
grep -n "CurveEditor\|WaveformView" CMakeLists.txt        # 只在 LEGACY 块里
nm -C build-release/pvforge | grep -c CurveEditor         # 0

# dist 验收命令不可执行
strings dist/pvforge | grep -c -- --self-test             # 0
timeout 5 ./dist/pvforge --self-test; echo $?             # 124

# CI 只有行数门禁
sed -n '1,30p' .github/workflows/ci.yml

# 死代码仍被构建
grep -n "add_subdirectory(src/next" CMakeLists.txt        # properties/timeline 无条件
grep -n "pvf_next_properties" build-release/CMakeFiles/pvforge.dir/link.txt  # 无

# 热路径拷贝排序
sed -n '24,36p' src/model/Keyframe.cpp

# 每次编辑深拷贝工程
sed -n '44,50p' src/next/app/ProjectDocument.cpp

# 假接口
sed -n '160,178p' src/ui/TimelinePanel.h

# 测试体系分裂
ctest --test-dir build-coord-tests -N | grep -c self-test   # 0
grep -rn 'registry.add(QStringLiteral("--' src/tests | wc -l  # 100
```
