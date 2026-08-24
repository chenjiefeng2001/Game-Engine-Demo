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

### GP-006
```text
Title:               敌人查询依赖固定名册
Category:            Query
Severity:            MEDIUM（前瞻）
Game Context:        GP1-B 战斗循环：玩家攻击需找范围内敌人、导航需找最近活敌
Observed Problem:    现有手段 = Engine.entity.find(名字) × 固定名册遍历。
                     场景实体静态时可用；一旦 GP1-C 引入运行时 Spawn，
                     名册无法预知新实体 —— 查询链路断裂。
Current API Attempt: entity.find 逐名查询 + Lua 表维护（本阶段实际做法）。
Why Current API Is Insufficient: 动态生成场景下无解；静态规模下足够。
Workaround:          ENEMY_ROSTER 静态名册 + ipairs 遍历距离排序。
Frequency:           每帧（攻击选 target + 导航）。
Production Cost:     当前 5 实体可接受；随实体数线性上升。
Cross-Game Evidence: VS01 同模式（固定 5 敌名册）—— 两局同信号，但触发条件
                     （动态 Spawn）尚未出现。
Decision:            OBSERVE —— GP1-C Spawn 落地时自动复核；
                     若确认断裂，走最小 API 提案（如 entity.find_all/tag），
                     不直接跳 ECS。
Level:               L0（前瞻登记）
```

### GP-005
```text
Title:               M003 Prefab 再评价（手写多类型实体的真实成本）
Category:            Workflow / Content
Severity:            LOW
Game Context:        GP1-B1 在 Main.scene 手写 Grunt×2/Tank×1/Scout×2
                     共 5 个敌人 JSON 条目（名称/GUID/坐标三行×5）。
Observed Problem:    无阻塞。重复成本 ≈ 每敌 2 分钟、复制 GUID 时需小心
                     类型映射（grunt/tank/scout 各自 sprite GUID 不同）。
Current API Attempt: 直接手写场景 JSON（章程 §11 指定的故意测试项）。
Why Current API Is Insufficient: 不成立 —— 当前规模完全可接受。
Workaround:          —
Frequency:           一次性内容生产成本。
Production Cost:     低。若 GP1-C 动态 Spawn 使"场景内摆实体"本身失效，
                     本条目性质将变化（由编辑成本转为生成机制问题）。
Cross-Game Evidence: VS01 五敌同模式。
Decision:            DEFERRED 维持（M003 不做）；GP1-C 复核。
Level:               L0
```

### GP-004
```text
Title:               M002 Component Serialization 再评价（战斗状态存续）
Category:            Serialization / Save
Severity:            —（验证通过，非摩擦）
Game Context:        GP1-B5 要求 Score/HP/敌我生死/掉血穿越 Save→Kill→
                     Restart→Load 全等恢复。
Observed Problem:    无阻塞。方案 = 场景序列化(位置/精灵) ＋ 游戏自有
                     GameStateEncode/Restore 编码串 ＋ 宿主经 _PERSIST
                     中介回灌。格式归 game.lua 所有，引擎零改动。
Current API Attempt: SceneSerializerV1 + ScriptInstance._PERSIST（冻结契约内）。
Why Current API Is Insufficient: 不成立 —— RestartCombatState 证明全等恢复
                     （blobA==blobB）、死者仍死、可继续打到 Victory。
Workaround:          即上述编解码（它不是 workaround，是游戏代码的正常组成）。
Frequency:           每次 Save/Load。
Production Cost:     每类新增持久化字段 +1 行编码 +1 行解析（线性、可控）。
Cross-Game Evidence: VS01 boss 幕 _PERSIST 中介先例。
Decision:            DEFERRED 维持（M002 不做）。启动阈值维持章程 §9：
                     仅当数据必须进 Inspector/Scene 编辑时再议。
Level:               L0（结论性证据条目）
```

### GP-003
```text
Title:               M004 Input.pressed() 再评价（攻击边沿语义）
Category:            Input
Severity:            —（验证通过，非摩擦）
Game Context:        GP1-B3 战斗攻击需要"按一下打一次 / 按住按冷却连击"。
Observed Problem:    无阻塞。is_down('J') + 冷却门控天然给出两种语义：
                     单帧按下=一刀；持续按住=cd 节拍连击
                     （CombatDamage 测试分别断言）。
Current API Attempt: Engine.input.is_down（冻结 API）。
Why Current API Is Insufficient: 不成立。若未来需要"精确按下次数计数"
                     （如连段），Lua 侧 prevHeld 边沿跟踪亦可表达。
Workaround:          —
Frequency:           每帧。
Production Cost:     零额外。
Cross-Game Evidence: VS01 menu RETURN 边沿同模式。
Decision:            DEFERRED 维持（M004 不做）。
Level:               L0（结论性证据条目）
```

### GP-002
```text
Title:               GP1-B 基线锁定声明
Category:            Process / Baseline
Severity:            —（非摩擦条目，证据锚点）
Game Context:        GP1-B Core Gameplay 开工前（2026-08-24）
Observed Problem:    无 —— 记录"开始前冻结 API 足以表达当前 gameplay"。
Current API Attempt: Scripting v2.1 / Content v1 / Lifecycle v1 / Editor v1 /
                     Physics v1.x / Save-Load 全部维持冻结。
Why Current API Is Insufficient: 尚无证据；本条目为后续任何新增 API 提供
                     证据起点。
Workaround:          —
Frequency:           一次性。
Production Cost:     —
Cross-Game Evidence: —
Decision:            OBSERVE
Level:               L0
```

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
| GP-006 | 敌人查询依赖固定名册 | Query | MEDIUM(前瞻) | L0 | OBSERVE（GP1-C 复核） |
| GP-005 | M003 Prefab 再评价（手写 5 实体成本） | Workflow | LOW | L0 | DEFERRED 维持 |
| GP-004 | M002 再评价（战斗状态存续方案成立） | Serialization | — | L0 | DEFERRED 维持 |
| GP-003 | M004 再评价（is_down 边沿语义够用） | Input | — | L0 | DEFERRED 维持 |
| GP-002 | GP1-B 基线锁定声明 | Process | — | L0 | OBSERVE |
| GP-001 | 玩法代码多文件组织不可表达 | Workflow | LOW | L2(记录) | OBSERVE |

### GP-001 增补（GP1-B 后）

game.lua 增长至 ~200 行（数据表+AI+战斗+状态机+存档编解码），仍单文件可维护。
分区手段：全局 `ENEMY_TYPES` 数据契约 + local 函数分组。晋升阈值继续观察：
预计 ~400 行或职责互相干扰时复核。
