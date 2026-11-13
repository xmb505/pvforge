# 实体/时间轴规范落地决定（2026-11-13）

> 来源：人类在对话中的明确选择；`docs/human/module-pseudocode/实体和时间轴.txt` 是原始规范。
> 本文只记录实现口径，不是新需求。

## 1. JSON 存储

- 每个根子树一个文件：`entities/<root-uuid>.json`
- 实体递归包含 `"children": [ ... ]`
- **同时保留 `parentUuid`**（人类选择）：嵌套关系与 `parentUuid` 必须一致，验证器校验双份真相。
- 子实体 `start/end` 必须是父实体的子集。
- UUID 全树唯一；根实体 `parentUuid` 为空；根文件名必须等于根 UUID。
- 格式版本升到 3；旧单文件/旧扁平目录没有用户，不做兼容导入。

## 2. 父区间跟随

- 字段 `followParentRange` 在**子实体**上，默认 false。
- true：子实体 `start/end` 等于直接父实体。
- 开启时：区间改为父区间；关键帧重基准保持绝对帧；因为合法数据子区间本来就在父区间内，不删 key。
- 父移动：相对 key 不动，绝对帧随实体平移。
- 父 trim start/end：走现有 `trimNode`，越界 key 在提交时删除。
- 子实体 reparent：跟随子实体立即改为新父区间，key 重基准，越界 key 提交时删除。
- 只跟直接父级，不隔代；每个实体各管自己的 flag。
- 验证器：`follow=true` 时子区间必须等于直接父区间。

## 3. 可见性

- `hidden` 只影响工作台/时间轴显示，**不再影响渲染/导出**。
- 渲染/导出只看 `visible`（可关键帧）。
- `inheritVisibility` 继续控制父级可见性继承。

## 4. 坐标系

- `Entity::space`：`parent`（默认）或 `canvas`。
- `parent`：现有行为，position/rotation/scale 相对父级。
- `canvas`：相对画布（中心原点、Y-up），不继承父级 transform；时间轴区间仍必须是父区间子集。
- `space` 参与序列化、验证、MCP schema。

## 5. 拓展参数

- 不再把 `extras` 当通用可编辑 map；按实体类型做 schema + 强类型参数。
- 第一阶段先做文字和矢量，其余类型保留现有字段；后续按类型补 schema。
