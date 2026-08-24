# GP01-B Core Gameplay — 阶段报告

> 日期：2026-08-24 · 章程：`docs/GP-P1-Charter.md` §7 GP1-B · 账本：`docs/GP01-Ledger.md`
> 结论：**核心战斗循环成立，冻结契约足以支撑；无新增 P0/P1 引擎缺口。**

---

## 1. 交付内容

### 游戏（assets/gp01，零 C++ 改动）
- `game.lua`（~200 行）：`ENEMY_TYPES` 数据表（grunt/tank/scout：HP/速度/伤害/
  接触半径/分值）、直线追击 AI、攻击（is_down+冷却：单击一刀/按住连击）、
  接触伤害、死亡/得分、playing→victory/lost 状态机、HUD、
  `GameStateEncode/Restore` 存档编解码。
- `Main.scene`：+5 敌人实体（手写 JSON）。

### 测试（test_gp01 3 → **13**）
| 新增 | 守护的契约 |
|------|-----------|
| EnemyTypes | 数据表属性正确；实例 HP 独立；模板不被污染 |
| EnemySpawn | Grunt×2/Tank×1/Scout×2；同型同精灵、异型异精灵 |
| EnemyMovement | scout(7.04) > grunt(4.4) > tank(1.98) @2s —— 差异仅来自数据 |
| CombatDamage | 范围门控 / 单击一刀 / 无输入不补刀 / 按住两刀@36f |
| EnemyDeathAndScore | alive=false、尸体冻结、score=15×2 精确累计 |
| GameOver | hp≤0 → lost（Console 注入濒死属合法调试动作） |
| Victory | 清场 → victory；score=120=Σvalue；无人存活 |
| SaveCombatState | 中间战局落盘内容契约 |
| RestartCombatState | 冷启全等恢复（blobA==blobB）、死者仍死、活敌复动、续打至 victory |
| **CoreGameplay** | **B6 Golden：冷启→移动→战斗→中场存档→冷启→全等→继续→Victory** |

## 2. 数值设计要点（确定性推演）

- ATK_RANGE 1.10 必须覆盖 tank 接触半径 0.65+0.40=1.05，否则 tank 不可近战；
- BASE_SPEED 2.2 使 scout(3.52) < 玩家(4.0)：风筝在数学上可行；
- 敌人在 d≤radius+PL_R 停止追击 → 停驻点与玩家攻击带的相对关系决定交换比；
- 玩家 hp14 / CONTACT_CD 1.0 为留余量的可赢配置（站桩全清理论最大承伤 ~11）。

## 3. 生产过程摩擦实录

| 现象 | 定性 | 处置 |
|------|------|------|
| 固定名册查询在动态 Spawn 下将断裂 | 引擎能力边界信号 → **GP-006** | OBSERVE，GP1-C 复核 |
| 站桩杀敌必被咬（真实战斗代价） | 正常玩法 | 测试剧本化补给 + 走位带 |
| 存档点=敌群集结点，读档即围殴 | 玩法现实 | 读档后重定位（正常玩家动作） |
| harness 导航 Clear() 误吞攻击键 | 测试侧 bug | 导航只动 WASD |
| 无站定控制的导航会撞进接触区硬换血 | 测试侧走位 | 攻击带 [0.90,1.00] 进退逻辑 |

后两条同时是生产观察素材：**距离管理完全可用 is_down+坐标表达**，
未触发 Collision Event 需求。

## 4. Deferred 再评价裁定（章程 §11）

| 项 | 证据 | 裁定 |
|----|------|------|
| M004 Input.pressed | GP-003：单击/连击语义均由 is_down+cd 表达 | 维持 ⏸ |
| M002 Component Serialization | GP-004：游戏自有编解码+_PERSIST 中介全等恢复 | 维持 ⏸ |
| M003 Prefab | GP-005：手写 5 实体成本可接受 | 维持 ⏸ |
| Entity Query | GP-006：静态名册够用；动态 Spawn 是触发条件 | OBSERVE |

## 5. 证据链

- test_gp01 **13/13 PASS** ×4 连跑稳定（含 ASAN 环境）；
- 全仓基线 **113/113**；Integrity Gate I1/I2/I3 ALL GREEN；
- Gameplay C++ 修改 = **0**；全部工作在 Lua/场景/测试层完成。

## 6. 判定

按章程 §16：**GP1-B 顺利完成且无新 P0/P1 阻塞 = 成功**——
当前冻结契约能够支撑更复杂的真实游戏。唯一前瞻项 GP-006（Query×Spawn）
将在 GP1-C Content Scale 第一时间复核。

**下一道门：GP1-C —— 问题从"功能正确"转为"内容生产成本是否开始成为瓶颈"。**
