# Game Production Phase 1 — 实施章程（工作章程）

> **阶段名称：** Game Production Phase 1
> **阶段代号：** GP-P1
> **起始锚点：** VS01 Closure `a0bdfc6`
> **基线：** 100/100 ＋ Evidence Gate I1/I2/I3 GREEN
> **目标：** 用冻结引擎契约生产一款完整可玩的中小型游戏，并从真实生产阻塞中决定下一项引擎能力。
>
> 本文件是 GP-P1 的最高工作章程；执行细则见 §7 阶段划分，裁定规则见 `docs/GP01-Ledger.md`。

---

# 1. 阶段目标

本阶段不以"增加多少 API"为成功标准。

唯一核心问题：

> **开发者能否使用当前冻结契约，持续生产一款真正的游戏？**

VS01 已经证明：

> Engine **可以**完成一条完整产品链。

GP-P1 要进一步验证：

> Engine **能否支持持续的游戏开发迭代**。

因此本阶段的验证对象从：

```text
Capability Validation
```

切换为：

```text
Production Validation
```

---

# 2. 阶段原则

## 2.1 No Capability Without Evidence

继续作为最高原则。

任何新 Engine API / subsystem / abstraction 都必须来自：

```text
真实游戏需求
    ↓
实际阻塞
    ↓
可复现
    ↓
现有契约无法合理表达
    ↓
最小 Capability
```

否则不实现。

## 2.2 不提前解决 Deferred

当前：

```text
M002 Component Serialization    ⏸
M003 Prefab                    ⏸
M004 Input.pressed()           ⏸
M006 CWD anchor                ⏸
RenderGraph                    ❌ ISOLATED
```

GP-P1 开始时全部保持原状态。**不得因为"可能有用"提前实现。**

## 2.3 Gameplay 优先，Engine 改动最小化

游戏代码（Lua / Scene / Assets / Editor）优先解决问题。
只有确认冻结 API 无法合理解决，才进入 GP Ledger。

---

# 3. GP-P1 游戏选择

第一款正式生产游戏：

# GP-01 — Top-down Arena Survival

选择理由：同时验证 Player 控制、多实体、AI、战斗、HP、Damage、Pickup、Score、
Timer、Win/Lose、HUD、Asset Workflow、Script Reload、Save/Load、多场景、长时间迭代，
又不强迫引擎预先拥有大型 RPG / ECS / Prefab 架构。

与 VS01 的关系：VS01「Arena Trials」是**一次性验证门**（V1–V10 单向跑通）；
GP-01 是**持续生产对象**——同一题材在更长周期、更多迭代、编辑器驱动内容生产下
重新接受检验，摩擦记录口径完全不同（GP Ledger vs 缺陷账本）。

---

# 4. GP-01 MVP

## 4.1 玩家

```text
Player
├── 移动
├── HP
├── 攻击
├── 受伤
└── 死亡
```

## 4.2 敌人

至少三种：Grunt / Tank / Scout —— 使用 **Lua 数据表表达差异**，
不因重复实体提前引入 Prefab。

## 4.3 游戏循环

```text
Start → Spawn enemies → Player survives / attacks → Collect pickups
→ Score increases → Timer progresses → Enemy pressure increases
→ Win / Lose → Restart
```

---

# 5. 场景规划

第一版建议 `Main.scene` / `Arena.scene` / `Result.scene`；
若实际制作发现单场景足够，则不强制三场景。

> 原则：**验证真实需求，而不是为了验证多场景而制造多场景。**

# 6. 资产规划

第一批资产：

```text
Textures: player, enemy_grunt, enemy_tank, enemy_scout, pickup, wall, goal, arena
Scripts:  player.lua, enemy.lua, combat.lua, game.lua, pickup.lua
```

全部经过 ContentRegistry → GUID → SceneSerializerV1 → ResourceLifecycle。
**Lua 不得通过路径加载资源。**

---

# 7. 开发阶段划分

| Phase | 名称 | 目标 | 关键验收 |
|-------|------|------|---------|
| GP1-A | Project Bootstrap | 空场景建立游戏骨架 | 空场景→Player→Play→Save→Restart→Load→Player 正常运行 |
| GP1-B | Core Gameplay | 移动/敌人/AI/HP/伤害/死亡/Score | 不允许因方便扩充 API；摩擦全部进 Ledger |
| GP1-C | Gameplay Loop | Spawn/Timer/Difficulty/Pickup/Win/Lose/Restart | 观察 Query/Collision/Timer 三类摩擦 |
| GP1-D | Content Production | 编辑器连续生产内容 | 目标是"开发者能不能连续使用它"，记录操作步骤/重复操作/错误恢复等 |
| GP1-E | Production Iteration | ≥3 个完整迭代循环 | 每次迭代记录真实阻塞 |
| GP1-F | Polish | HUD/音效/动画/数值/UI/反馈 | 仍禁止为 polish 扩充 Engine API |

GP1-C 重点观察的三个摩擦信号（潜在新能力的来源）：

- **Query friction**：「我需要查询所有 Enemy。」
- **Collision friction**：「距离检测已经变成不可维护的复杂度。」
- **Timer friction**：「手动维护几十个 timer 已经成为真实生产成本。」

GP1-D 记录项：操作步骤、重复操作、手工维护点、错误恢复、资产定位时间、
Script Reload 次数、Save/Load 次数。

GP1-E 迭代结构：

```text
Iteration 1: Prototype → Play → 发现问题
Iteration 2: 修改 gameplay → Reload → Play → 发现问题
Iteration 3: 修改内容/数值/资产 → Save → Restart → Play
```

---

# 8. GP Ledger 设计与晋升规则

Ledger 文件：`docs/GP01-Ledger.md`。条目格式与四级晋升规则见该文件头部定义。

核心纪律：

- **Level 0 Friction**：发现问题，不改引擎；
- **Level 1 Reproducible**：同场景/同操作/同结果稳定复现；
- **Level 2 Capability Gap**（硬闸门）：证明现有冻结 API 组合无法合理表达才可晋升；
- **Level 3 Cross-game Evidence**：同类问题跨游戏复现（≥2 局）自动获得晋升资格。

新能力实施流程（禁止"看到问题→直接写 Engine"）：

```text
GP Ledger → Capability Proposal → 最小 API Contract → Contract Test
→ Implementation → Gameplay Revalidation → Documentation → Freeze / Defer
```

---

# 9. 特别关注的四个 Deferred

| 项 | 启动条件 |
|----|---------|
| M002 Component Serialization | HP/Damage/Speed 等必须作为实体 authored data 保存、Lua 常量方案成为明显生产负担时 |
| M003 Prefab | 不是"实体很多"，而是 Create Enemy #1..#10 的**重复生产成本**成为主要开发瓶颈时 |
| M004 Input.pressed | Jump/Shoot/Interact/Dash/Confirm 等 edge-trigger 需求真实出现、`is_down()`+Lua 状态不再合理时 |
| M006 CWD | Editor/Test/Sandbox/Packaged 因工作目录依赖产生**真实错误**时 |

# 10. RenderGraph 策略

维持 ISOLATED。GP01 使用已验证的 SpriteBatch/direct rendering。
只有"真实复杂 Pass Dependency + 直连 Renderer 成为阻塞"才重开 F1–F6。

# 11. 测试策略

每次 Engine 变化必须走完整链：Integrity Gate → 四套件 → E2E → Dogfood/GP test → 全量回归。
不接受"本地跑了一下看起来没问题"。I1/I2/I3 持续维持。

新增测试层 `test_gp01`——测试的是**游戏生产契约**而非引擎功能：
`GP01_Load` / `GP01_PlayerMovement` / `GP01_Combat` / `GP01_Win` / `GP01_Lose` /
`GP01_SaveLoad` / `GP01_Restart` / `GP01_LongSession`（随阶段逐个点亮）。

最终测试分层：Engine Tests ＋ Contract Tests ＋ Evidence Tests ＋ **Game Production Tests**。

---

# 12. 完成标准

### Gameplay
- [ ] 完整游戏循环 / Win / Lose / Restart
- [ ] 至少三种敌人行为
- [ ] 玩家状态（HP/受伤/死亡）
- [ ] 资源收集或成长

### Content
- [ ] 所有资产通过 ContentRegistry；Scene 使用 GUID；无路径泄漏
- [ ] 多次 Save/Load 正常；冷启动恢复

### Editor
- [ ] 从空场景开始：Asset Browser / Inspector / Script Editor / Console / Import / Reload

### Engineering
- [ ] Gameplay C++ 修改 = 0
- [ ] 未绕过冻结契约
- [ ] 所有 workaround 登记
- [ ] 所有新增 capability 有 Ledger 条目
- [ ] 全量测试通过 + Integrity Gate GREEN

### Production
- [ ] ≥3 次完整迭代
- [ ] 记录实际生产时间与主要摩擦
- [ ] 每项摩擦有最终裁定
- [ ] 形成 GP01 Production Report

---

# 13. 最终报告结构与交付物

```text
docs/
├── GP01-Production-Report.md   （12 章节见骨架）
├── GP01-Ledger.md
├── GP01-Postmortem.md
└── GP01-Capability-Decisions.md
```

# 14. 阶段最终可能产生的三种结果

- **Result A — 当前契约足够**：游戏完成＋无严重阻塞 → **不做 M5，继续制作游戏**。
- **Result B — 有 1–2 个真实通用缺口**：最小 Contract → 实现 → 测试 → 游戏验证 → Freeze。
- **Result C — 架构性问题**：现有 Scene/Resource/Script/Editor 边界无法支撑生产规模
  → 才进入下一轮架构工程（最不希望提前假设的结果）。

# 15. 阶段成功的真正定义

最终不问「我们增加了多少功能？」而问：

> **一个开发者是否可以使用当前引擎，在没有修改 Engine Gameplay C++ 的情况下，
> 从空场景开始，把一款游戏真正做完？**

答案是 Yes：GP-P1 成功，且不需要 M5。
答案是 No：我们就拥有一条经过真实生产验证的、可复现的
**GP Ledger → Capability Contract** 证据链。

**一句话：下一阶段不是"做 M5"，而是"做 GP01"。**
