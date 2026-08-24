# GP01 Ledger — 真实生产摩擦账本

> 建立：2026-08-24（GP-P1 DAY 0）· 依据：`docs/GP-P1-Charter.md` §8
> 性质：GP-P1 期间一切引擎能力讨论的**唯一入口**。看到问题 → 直接写 Engine 是被禁止的。

---

## 晋升规则（四级）

| Level | 名称 | 准入 | 动作 |
|-------|------|------|------|
| L0 | Friction | 发现问题 | 只登记，不改引擎 |
| L1 | Reproducible | 同一生产场景＋同一操作＋同一结果，稳定复现 | 可附最小复现案例 |
| L2 | Capability Gap | **硬闸门**：证明 Engine API A ＋ API B ＋ Lua logic 无法合理表达 | 才允许进入实施流程 |
| L3 | Cross-game Evidence | 同类问题跨 ≥2 局游戏复现 | 自动获得晋升资格 |

新能力实施流程（L2 通过后）：

```text
GP Ledger → Capability Proposal → 最小 API Contract → Contract Test
→ Implementation → Gameplay Revalidation → Documentation → Freeze / Defer
```

## 条目格式

```text
GP-xxx
Title:
Category:            # Query / Collision / Timer / Serialization / Input / Workflow / ...
Severity:            # BLOCKER / HIGH / MEDIUM / LOW
Game Context:
Observed Problem:
Current API Attempt:
Why Current API Is Insufficient:
Workaround:
Frequency:           # 每帧 / 每次编辑 / 每次 Save ...
Production Cost:     # 定性 + （可能的）定量
Cross-Game Evidence: # 无 / VS01(同类信号) / GP-xx ...
Decision:            # OBSERVE / PROMOTE / DEFER-UNTIL <条件>
Level:               # 当前到达的晋升级别
```

## 与历史 Ledger 的关系

- DF01 缺口台账（M001–M006）：M001/M005 已实现并冻结；M002/M003/M004/M006 维持 ⏸，
  其再晋升条件以 `docs/GP-P1-Charter.md` §9 为准；
- VS01 缺陷账本（F1/F2/F-07）：F1 已修复并有回归守卫，不重复登记；
- 本账本只登记 **GP01 生产过程中新观察到的摩擦**。

---

## 条目区

### GP-001
```text
Title:               玩法代码多文件组织不可表达
Category:            Workflow / Scripting
Severity:            LOW（当前规模）
Game Context:        GP1-A 项目骨架搭建，章程 §6 规划 player/enemy/combat/
                     game/pickup 五个脚本模块
Observed Problem:    沙箱移除 dofile/loadfile/require（LuaEngine.cpp 沙箱
                     清单），场景仅支持单一 Director 脚本绑定；玩法代码只能
                     单文件增长。
Current API Attempt: 章程规划的多文件布局；RunFile 为宿主侧能力，Lua 内无
                     include 手段。
Why Current API Is Insufficient: 无 —— 当前单文件仍然合理（VS01 arena
                     导演 199 行可维护）。
Workaround:          每场景单文件导演；模块化靠 Lua 内 local 表分区注释。
Frequency:           每次"想拆文件"时（结构性，非每帧）。
Production Cost:     随玩法规模线性上升；预计 GP1-B 加入三种敌人 AI 后开始
                     显现。
Cross-Game Evidence: VS01（同类约束下同样被迫单文件）—— 已构成两局同信号，
                     但尚无生产成本证据支撑晋升。
Decision:            OBSERVE
Level:               L0→L2 已记录（缺口确凿），晋升等成本证据
```

### 裁定汇总表

| 编号 | Title | Category | Severity | Level | Decision |
|------|-------|----------|----------|-------|----------|
| GP-001 | 玩法代码多文件组织不可表达 | Workflow | LOW | L2(记录) | OBSERVE |
