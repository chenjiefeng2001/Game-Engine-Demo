# GP01-C Content Scale — 阶段计划与裁决框架

> 日期：2026-08-24 · 章程：`docs/GP-P1-Charter.md` §7 GP1-C · 前序：GP1-B（commit `9b75bba`）
> 核心问题：**当游戏从"能跑"扩大到"需要持续生产内容"时，现有 Content + Scene +
> Scripting + Editor 工作流是否仍然经济？**

---

## 0. 基线锁定（C0）

- 基线：113/113，I1/I2/I3 GREEN，工作树 clean @ `9b75bba`
- 本阶段纪律：**不为进入下一阶段而实现 Prefab / ECS / Collision Event / RenderGraph**

## 1. 规模目标（C1/C2）

| 维度 | GP1-B | GP1-C 目标 |
|------|-------|-----------|
| 运行时敌人 | 5（静态摆场） | **≥30（动态生成）** |
| 波次 | 无 | **5 波混合编成**（19G/6T/7S） |
| 场景 JSON 敌人条目 | 5 | **0**（仅 Player/Walls/SpawnPads/Director） |
| 注册表资产 | 9 | **≥30** |
| Gameplay C++ | 0 行 | **仍 0 行** |

关键反差实验：动态生成使场景作者成本**下降**（不再手写敌人条目），
运行时复杂度转移到 Lua —— 两者的净效应即 M003/Prefab 与 Query 的裁决素材。

## 2. 关键前置发现（决定实验形态）

`Engine.entity.spawn(name)->handle` / `entity.destroy(handle)`
**已在冻结 API v2.1 中**（GameplayAPI.cpp L_EntitySpawn/L_EntityDestroy）。
→ 动态生成实验零引擎改动；GP-006 的考察点收窄为：
**生成后的"实体账簿"维护成本**（Lua 侧自维护名册 vs 引擎侧查询）。

## 3. 测量项（C3）

① 查询/账簿成本：为找到"所有敌人"需要维护多少代码
（预期形态：Lua 自维护 `enemies` 表 + 生成名 `E_W{wave}_{nn}`）
② 内容重复率：场景 JSON 行数、Lua 行数、每敌边际作者行数
③ 脚本复杂度：波次推进/生成/死亡/计分的职责是否开始互相干扰

## 4. 测试契约（C4）

新增/迁移至 `GP01.ContentScale.*` 与改造后的战斗组：

- Load：33 资产 100% 解析；瘦场景实例化
- DynamicSpawn：≥20 生成体全部 valid、落点在 Pad 邻域、账簿一致
- MixedTypes：四型（含 boss）同场行为分异
- WavesProgression：五波编成逐波点亮、总数精确
- SaveMidWaves：Spawn→Kill→Spawn→Kill→Save→冷启→全等→续波可用

## 5. 作者体验测量（C5/C7，手工口径）

任务 A："增加第 31 个敌人" —— 预期：改 1 处波次表（+1 token），0 场景行。
任务 B："新增只出现在 Wave 6 的敌人类型" —— 预期：+1 数据表行 +1 纹理资产
+1 清单行 +1 波次表 token。
（编辑器内点击流不在本阶段 headless 范围 → 残留缺口记入 GP1-D。）

## 6. 裁决门（C6/C7/C8）

| 能力 | 晋升门 | 默认 |
|------|--------|------|
| Query (find_all) | 动态生成后名称账簿必须手工维护且无法用现有 API 组合消除 | DEFER |
| M003 Prefab | 同类≥10 且重复复制且一次结构修改传播≥5 实例 | DEFER |
| Collision Event | instruction budget 逼近上限或碰撞代码占比失控 | DEFER |
| M002 Comp. Serial. | 数据必须进 Scene/Inspector | DEFER |
| Asset Browser v1 | 33 资产出现检索/区分/批量摩擦 | 冻结维持 |
| Timer/Scheduler | 波次计时手工递减成为真实负担 | OBSERVE |

## 7. 完成标准（C10）

C0 锁定 ✅ · C1 ≥30 运行时实体 · C2 五波 · C3 动态生成 · C4 混合类型 ·
C5 中场存档 · C6 冷启恢复 · C7 作者测量 · C8 资产规模 · C9 四项裁决 ·
C10 Gate GREEN —— 全部达成后出 `GP01-C` 裁决表并定夺 GP1-D。

---

## 8. GP01-C 裁决表（C9）与 GP1-D 定夺

> 日期：2026-08-25 · 测试证据：test_gp01 **17/17**（基线 117/117，
> I1/I2/I3 GREEN，`tools/integrity_gate.ps1`）

### 8.1 完成标准对照（C0–C10）

| 项 | 标准 | 结果 | 证据 |
|----|------|------|------|
| C0 | 基线锁定 | ✅ | 113/113 @ `9b75bba`（§0） |
| C1 | ≥30 运行时实体 | ✅ 32 体全部经 `entity.spawn` 实体化 | WavesProgression：`spawned=32` |
| C2 | 五波逐波点亮 | ✅ 每波编成精确（19G/6T/7S） | WavesProgression 逐波断言 + WaveTableSpec |
| C3 | 动态生成 | ✅ 20 并发生成、Pad 邻域落点、账簿一致 | DynamicSpawn |
| C4 | 混合类型 | ✅ 四型同场速度阶梯/血量分异 | MixedTypes |
| C5 | 中场存档 | ✅ 波中增援序列 Save→冷启 blob 全等 | SaveMidWaves |
| C6 | 冷启恢复 | ✅ 幸存者原坐标重铸、死者仍死、续波可用 | SaveMidWaves |
| C7 | 作者测量 | ✅ 任务A = +1 token / 0 场景行；任务B 实做（boss 型）= +1 表行 +1 纹理 +1 清单行 +1 token | Ledger GP-007/GP-008 |
| C8 | 资产规模 | ✅ 注册表 33 资产 100% 解析（32 tex + 1 script） | GP01.Load |
| C9 | 裁决 | ✅ §6 六门全闭合 | §8.2 + Ledger GP-004~010 |
| C10 | Gate GREEN | ✅ | 117/117 · I1/I2/I3 GREEN |

### 8.2 能力裁决（§6 六门闭合）

| 能力 | 触发条件是否出现 | 裁决 | 证据 |
|------|------------------|------|------|
| Query (`find_all`/tag) | 否 —— Lua 自维护账簿实测可控：Materialize 单点 ~10 行 + Encode/Restore ~50 行，随持久化字段线性 | **DEFER 维持** | GP-006（L0→L1 复核收敛） |
| M003 Prefab | 否 —— ENEMY_TYPES 数据表一次修改天然传播全实例，"结构重复传播"未发生 | **DEFERRED 维持** | GP-005 + GP-006 增补 |
| M002 Comp. Serial. | 否 —— 存档 v3 纯游戏自有格式；Inspector/Scene 编辑需求未出现 | **DEFERRED 维持** | GP-004 |
| Collision Event | 否 —— 手写圆碰撞三处共 ~20 行（墙推挤/接触伤害/攻击范围），33 实体帧成本不可见 | **DEFER 维持** | GP-010 |
| Asset Browser v1 | 未触发 —— headless 无点击流；前缀命名约定在清单层足够定位 | **冻结维持**（编辑器侧复核顺延 GP1-D） | GP-009 |
| Timer/Scheduler | 否 —— 全部计时仅 3 个手工递减量（startGrace/atkCd/hitCd），非真实负担 | **OBSERVE 维持** | game.lua 波次推进/攻击/接触三处 |

### 8.3 净效应结论（M003/Prefab 与 Query 的反差实验）

动态生成使场景作者成本**下降**（敌人 JSON 条目 5→0），运行时复杂度转移到
Lua 波次表（每敌边际 +1 token）。新增的一次性投资集中在两处：
生成账簿（`Materialize` 单点登记）与存档编解码（`GameStateEncode/Restore`）。
两者均随规模**线性**而非超线性增长 —— 在 32 体/33 资产规模下不构成任何
引擎能力晋升证据。

### 8.4 GP1-D 定夺

**进入 GP1-D（Content Production：编辑器工作流验证）。**

理由：
- 四个 DEFER 门（Query/Prefab/M002/Collision）在 headless 生产下全部无摩擦，
  继续在纯脚本侧挖证据已无信息增量；
- 剩余未知集中于编辑器人因（Asset Browser 检索、点击流、Reload 循环），
  正是 GP1-D 的考察范围；GP-009/GP-001 的编辑器侧复核自然顺延至此。

---

## 8. 结果记录（2026-08-25 结项）

### 8.1 规模目标达成

| 维度 | 目标 | 实测 |
|------|------|------|
| 运行时敌人 | ≥30 动态生成 | 五波编成 32 体；测试实测 ≥20 体同场全 valid |
| 波次 | 5 波混合编成 | 5 波（19G/6T/7S）逐波点亮 ✅ |
| 场景 JSON 敌人条目 | 0 | **0**（Main.scene 仅 10 个布局对象） |
| 注册表资产 | ≥30 | **33** |
| Gameplay C++ | 0 行 | **0 行**（全部冻结 API：entity.spawn/find/transform/input/ui/log） |

### 8.2 测量（C3/C5/C7）

① 账簿成本：game.lua 自维护 enemies 表 + E_NNN 计数器。生成点
   Materialize ~10 行；存档编解码 Encode/Restore 各一段合计 ~50 行；
   随持久化字段线性增长。引擎侧零改动。
② 内容重复率：场景敌人条目 5→0；每敌边际作者成本 = +1 波次表 token。
   任务 A 实测（+第 31 敌）：改 WAVES 一处、0 场景行 ✅；
   任务 B（新类型）：+1 ENEMY_TYPES 行 +1 纹理 +1 清单行 +波次 token ✅。
③ 脚本复杂度：game.lua 200→313 行，职责五区（数据/生成/存档/AI/状态机）
   无互相干扰；单文件维持（GP-001 增补，阈值 ~400 行复核）。

### 8.3 裁决门落定（C6/C9）

| 能力 | 门条件 | 实测 | 裁定 |
|------|--------|------|------|
| Query find_all | 账簿手工维护且无法用现有 API 消除 | 可维护且可控（~60 行一次性成本），17 测试全绿 | **DEFER 维持** |
| M003 Prefab | 同类≥10＋结构修改传播≥5 | grunt×19 经 ENEMY_TYPES 数据表天然传播 —— Lua 数据已消除该需求 | **DEFERRED 维持** |
| Collision Event | instruction budget 逼近/碰撞占比失控 | 接触伤害 ~20 行 / 313 总行；headless 7200 帧无预算问题 | **DEFER 维持** |
| M002 Comp. Serial. | 数据必须进 Scene/Inspector | blob+_PERSIST 中介继续成立（波间冷启全等） | **DEFERRED 维持** |
| Asset Browser v1 | 33 资产检索摩擦 | 前缀命名约定足够；点击流证据缺位 | **OBSERVE → GP1-D** |
| Timer/Scheduler | 手工递减成为真实负担 | 仅 startGrace 一个计时；波次用"清场触发"非墙钟 | **OBSERVE 维持** |

### 8.4 测试证据（C4）

test_gp01 13→**17**：ContentScale_DynamicSpawn / MixedTypes /
WavesProgression / SaveMidWaves 全 PASS。
全量回归 **117/117**；Integrity Gate I1/I2/I3 **ALL GREEN**
（基线 `docs/Evidence-Baseline.md` 已同步 113→117）。

### 8.5 GP1-D 定夺输入

- 编辑器工作流（Asset Browser 点击流/Inspector/Reload 循环）是唯一
  未覆盖的测量面 → GP1-E 主线（章程 §5）按原计划推进；
- 本阶段四项能力门全部 DEFER/OBSERVE，无晋升 —— 冻结 API v2.1 在
  "内容规模 ×10"冲击下再次全程零改动存活。
