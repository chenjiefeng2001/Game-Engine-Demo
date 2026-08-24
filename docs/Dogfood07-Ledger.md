# Dogfood-07 可维护性 Friction Ledger

> 日期：2026-08-24 · 游戏：Tower Defense Lite（10 实体：Player + 3 Turret + 5 Gem + Base）
> 约束：仅使用冻结 Scripting API v2.1，零 C++ 修改
> 验证目标：**可维护性**而非规模 —— 三类真实成本观测

---

## 结论先行

**未出现 P0/P1 阻塞。** 三个观察维度均有摩擦信号，但均未达到"阻塞下一款游戏制作"的门槛。
按 *No Capability Without Evidence* 原则：**所有 deferred 项维持 deferred，不启动任何新系统。**

```text
75/75 PASS（scripting 23 · physics 16 · renderer 9 · content 27）
```

---

## 维度一：重复代码成本（Prefab 候选）

| 观测点 | 结果 |
|--------|------|
| 3 座 Turret 行为 | ✅ 单脚本循环 + 属性表解决，无需复制 Lua |
| 5 个 Gem 行为 | ✅ 同上，数组遍历即可 |
| 场景内相似实体创建 | ⚠️ JSON 手写 10 个实体块，每个 ~60 字节 |

**判定**：当前规模（≤15 实体）下复制粘贴场景 JSON 的成本可接受。
Turret 差异仅是数据差异（range/dmg/cooldown），Lua 表驱动完全够用。

**M003 Prefab 维持 deferred。** 晋升条件：单场景 >30 相似实体，或跨场景复用行为包。

## 维度二：数据表达成本（Component Serialization 候选）

| 观测点 | 结果 |
|--------|------|
| Turret range/dmg/cooldown | ⚠️ 硬编码在脚本顶部常量区 |
| Enemy HP/speed | ⚠️ 同上 |
| Player speed / Gem 半径 | ⚠️ 同上 |

脚本头部出现了 **11 个调参常量**：

```lua
local PLAYER_SPEED = 7.0
local GEM_R        = 0.50
local ENEMY_HP     = 2
local ENEMY_SPEED  = 2.0
...
```

**判定**：这是 DF06→DF07 最明显的摩擦信号——每次调参都要改脚本并重启。
但注意：**Script Editor（M4-B）已经把"改脚本"的成本降到秒级**，且 `_PERSIST`
支持热重载保留状态。真正被阻塞的是"非程序员调参"场景，目前不存在该用户。

**M002 Component Serialization 维持 deferred。** 晋升条件：需要 Inspector 内实时调参
且脚本重启成本成为实测瓶颈（当前 Script Editor 循环已覆盖）。

## 维度三：事件/查询成本（Collision Events / Entity Query 候选）

| 观测点 | 结果 |
|--------|------|
| Gem 收集检测 | ✅ 5 次/帧距离扫描，O(N) 无感 |
| 敌人索敌（若启用 Turret 攻击） | ⚠️ O(T×E) 每帧扫描，T=3 E≤20 时 ~60 次 sqrt/帧 |
| 碰撞响应 | ✅ 距离检测 + 手动状态翻转 |

**判定**：600 帧 headless 运行 instruction budget 余量充足（5M 上限未触发）。
每帧 ≤100 次距离计算在当前实体规模下不构成性能或代码复杂度问题。
DF06 的 15 实体战斗同样验证了这一点。

**Collision Events 维持 deferred。** 晋升条件：实体对检测数 >1000 对/帧，
或事件驱动逻辑（on_enter 回调语义）成为玩法设计的硬需求。

### 新增低优先级观测（不立项）

- **DX09 Timer/Scheduler**：敌人生成需要手动 spawnTimer 累减。若后续游戏出现
  ≥3 处独立计时器逻辑，考虑 `Engine.timer.after(sec, fn)` 微契约。

---

## 流程摩擦（非引擎能力）

本次发现并修复了一个**测试基建缺陷**而非引擎缺陷：

1. `Dogfood05Test.cpp` 此前从未加入 CMakeLists——DF05 冒烟实际由
   ContentPipelineTest 内嵌版本承载；独立文件是死文件
2. DF07 补建独立测试时暴露缺 include、test_content 未链接 lua 库等问题，
   已通过 RunString+GetGlobal 模式规避直接 lua API 依赖

教训入账：**新增 Dogfood 测试文件必须同步登记 tests/CMakeLists.txt 并确认编译产物计数变化**
（本次 25→27，两文件各贡献 1 test）。

---

## Ledger 总表更新

| 条目 | 状态 | 变化 |
|------|------|------|
| M002 Component Serialization | ⏸ deferred | 维持（信号↑ 但未达阈值） |
| M003 Prefab | ⏸ deferred | 维持 |
| Collision Events | ⏸ deferred | 维持 |
| DX09 Timer/Scheduler | 👁 观察 | **新增**（无实现承诺） |
| 其余冻结契约 | 🔒 | 不变 |
