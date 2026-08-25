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

## GP-DX 条目区（GP1-D Editor Workflow，2026-08-25 起）

> 证据来源：`tests/test_gp01/GP01EditorWorkflowTest.cpp` 四契约
> （commit `222af42`，基线 121/121）。七元组口径见
> `docs/GP01-D-Editor-Workflow-Plan.md` §2。

### GP-DX-001
```text
Title:               跨工程上下文切换无自动恢复
Category:            Workflow / Editor Context
Severity:            LOW（真实编辑器单场景不触发）
Game Context:        GP1-D RenameSafety：冷启验证工程 B 后返回工程 A 继续编辑
Observed Problem:    GameplayAPI 全局场景/输入绑定被新 ctx 的 BeginFresh 抢占，
                     返回原工程后 HandleSpawn 静默落入他场景 —— 需手动三连
                     Reset/SetScene/SetInputProvider 重绑。
Current API Attempt: 手动三连重绑（可行但易忘；无"当前工程"概念）。
Why Current API Is Insufficient: 多工程并行时无自动恢复语义。
Workaround:          显式重绑三行（测试内已固化）。
Frequency:           每次多工程切换（headless 测试 / 未来多窗口）。
Production Cost:     单场景编辑器 = 0；多工程 = 每次切换 3 行 + 遗忘风险。
Cross-Game Evidence: 无（DX02 同家族信号：全局绑定需手动维护）。
Decision:            OBSERVE —— 真实编辑器出现多场景/多窗口工作流时复核。
Level:               L0
```

### GP-DX-002
```text
Title:               重名实体按名选择静默错选（DX05 二次复现）
Category:            Editor / Entity Identity
Severity:            MEDIUM
Game Context:        GP1-D 同名探针：连续创建两只 "Crate" 并分别摆位/贴图
Observed Problem:    创建流程内按名定位命中首个同名旧实体 —— 第二只的
                     set_position/SetTexture 静默落到第一只身上；
                     存储层无损（索引对齐，双条目独立落盘），歧义纯在
                     选择层。测试改用插入索引定位绕开。
Current API Attempt: Scene::FindObject(name)（首中语义）/ 按名 idx 映射。
Why Current API Is Insufficient: name 是查询便利而非持久身份（GUID 才是），
                     重名时一切按名操作无二义性保证。
Workaround:          插入索引定位（创建流）；或保证命名唯一（纪律）。
Frequency:           每次重名实体的选择/绑定/查找。
Production Cost:     存储零损失；选择层每次重名即隐患，静默难排查。
Cross-Game Evidence: DX05（DF04 原始记录：SaveScene 绑定按名对齐歧义）
                     —— 两局同信号成立。
Decision:            OBSERVE → L1（跨阶段复现达成）。仍不引入 Entity UUID
                     UX；晋升阈值 = 出现一次因重名导致的真实内容事故。
Level:               L0→L1
```

### DL-01 / DL-02 判决（GP1-D 正面实测）

| 项 | 判决 | 证据 |
|----|------|------|
| DL-01 活场景契约 | **CONFIRMED**：CaptureScene 存运行时位置（精确=teleport 落点），非 authored 值；漂移经 Save→冷启原样穿越 | IterationLoops It.2/It.3 断言 |
| DL-02 绑定表依赖 | **CONFIRMED**：mid-session 新增实体未经绑定表同步，保存后 sprite/script GUID 静默置 Null；同步 push 后全程存活 | IterationLoops It.2 双向断言 |

编辑器 Save 语义因此完全可解释：**Save = 当前活场景定格快照**
（含运行漂移），不是 authored 状态回滚。开发者心智模型 =
"PristineSave 先于战斗"；成本可控，暂无晋升动作。


### GP-DX-003
```
Title:               Production UI Workflow Gap（人工生产验证阻塞）
Category:            Editor / Production UI
Severity:            BLOCKER（对 GP1-D Human Run 而言）
Game Context:        GP1-D 收尾裁决：自动化契约 4/4 GREEN，但人工完成 Arena v2
                     改造无法成立 —— 生产入口未闭环。
Observed Problem:    开发者无法仅用 Editor UI 完成 GP01 v1→v2 的修改与验证，
                     必须依赖内部 API / 手改文件。断点集中在接线层：
                     资产分配三条 DND 通路全断、ContentRegistry 与主编辑器
                     零连接、脚本编辑器为死代码、选择同步缺一环。
Current API Attempt: 全部生产能力已在冻结契约层被四契约证明（GoldenPath 等），
                     缺的仅是 UI 入口 —— Engine Capability 不是瓶颈。
Why Current API Is Insufficient: 不适用（API 充足，UI 层缺失）。
Workaround:          无（这正是阻塞本身）。
Frequency:           每次内容生产尝试。
Production Cost:     人工生产验证完全不可行。
Cross-Game Evidence: VS01（ScriptSandbox 单页形态已暴露同类需求）。
Decision:            PROMOTE —— 触发 GP1-DX / Production UI Foundation：
                     最小 UI 闭环（工程加载/内容分配/Hierarchy-Inspector-
                     Viewport 联动/Save/Play/Stop），目标 = 仅用 Editor 完成
                     GP01 v1→v2。不做完整 Unity/Unreal。
Level:               L2（晋升资格成立）
```

### GP-DX-004
```
Title:               Editor/Play 双场景状态一致性语义
Category:            Editor / State Model
Severity:            MEDIUM（前瞻）
Game Context:        GP1-DX Play = SceneSerializerV1 克隆 + 冻结 API 驱动；
                     编辑态与运行态并存。
Observed Problem:    待人工验证：Play 中修改 → Stop → Save，开发者预期与
                     实际保存物是否一致。
Current API Attempt: 设计语义已定：Save 恒存编辑态；Stop 整体丢弃克隆。
Why Current API Is Insufficient: 不适用（语义明确，风险在心智模型）。
Workaround:          —
Frequency:           每次 Play 后保存。
Production Cost:     若语义被误解则产生"改动丢失感"。
Cross-Game Evidence: 无（DL-01 同族：Save=活快照）。
Decision:            OBSERVE —— GP1-D-Human-Run.md Run3 步骤6 实测裁决。
Level:               L0
```

### GP-DX-005
```
Title:               Viewport 仅 Billboard 反馈（Sprite 不可视）
Category:            Editor / Rendering Feedback
Severity:            LOW
Game Context:        GP1-DX P0 已知限制（计划 §3）。
Observed Problem:    无法在视口直接判断贴图绑定正确性/位置遮挡关系。
Current API Attempt: Content 面板 + Inspector 文案辅助判断。
Why Current API Is Insufficient: 待证据 —— 若人工 Run 判定受阻即升级 P1。
Workaround:          面板文案核对。
Frequency:           每次 Assign。
Production Cost:     低-中（取决于 Run2 实测）。
Cross-Game Evidence: 无。
Decision:            OBSERVE（升级标准见 GP1-D-Human-Run.md）。
Level:               L0
```

### GP-DX-006
```
Title:               Project Session 生命周期（重复加载/反复 Play）
Category:            Editor / Session
Severity:            LOW（前瞻）
Game Context:        Open Project 可重复触发；Play/Stop 可无限循环。
Observed Problem:    待实测：重复 Load 是否泄漏 Registry/Scene；
                     EventBus 是否重复注册；句柄是否累积。
Current API Attempt: 代码层预检干净 —— Load 全量替换并 GameplayAPI::Reset，
                     Hierarchy.Init 仅启动一次。
Why Current API Is Insufficient: 不适用。
Workaround:          —
Frequency:           高频（每次重开工程/每局 Play）。
Production Cost:     泄漏则随会话时长劣化。
Cross-Game Evidence: GP-DX-001 同族（全局状态生命周期）。
Decision:            OBSERVE —— Human Run 操作矩阵实测。
Level:               L0
```
### Dogfood04 DX 台账复核（DX02/03/05/07）

| 编号 | 复核结果 | 处置 |
|------|----------|------|
| DX02 输入手动绑定 | 不变 —— 仅 headless 相关（编辑器宿主自动绑定）；GP-DX-001 为其多工程延伸 | 维持 |
| DX03 导入新资产 | Import API 幂等自然（GoldenPath G2 直通）；文件对话框属 UI 层缺失，headless 无摩擦 | 维持 |
| DX05 同名歧义 | **二次复现**（存储安全、选择层静默错选）→ 见 GP-DX-002 | L1 |
| DX07 Import-before-attach | 本阶段未构造反序场景，未复核 | 留待 GP1-E |

---

## 条目区

### GP-010
```text
Title:               Collision Event 再评价（手写圆碰撞的规模成本）
Category:            Collision
Severity:            —（验证通过，非摩擦）
Game Context:        GP1-C 33 实体同场：墙推挤/敌人接触伤害/攻击范围判定
                     三处碰撞逻辑（game.lua，dist() 圆检查合计 ~20 行）。
Observed Problem:    无阻塞。32 体每帧 ~32 次距离计算 + 4 墙推出，
                     无 instruction 预算压力；碰撞代码占 game.lua ~6%。
Current API Attempt: Lua 手写圆碰撞（章程指定的故意测试项，非 workaround）。
Why Current API Is Insufficient: 不成立 —— 晋升条件（instruction budget
                     逼近上限或碰撞代码占比失控）均未出现。
Workaround:          —
Frequency:           每帧。
Production Cost:     新增一类碰撞 ≈ +5~8 行（圆参数 + 响应分支），线性可控。
Cross-Game Evidence: VS01 同模式（接触伤害/拾取均为圆检查）—— 两局同信号
                     且均无摩擦。
Decision:            DEFER 维持。复核条件：实体数 × 碰撞对帧成本进入
                     profiling 可见量级，或碰撞形状需求超出圆的表达范围。
Level:               L0（结论性证据条目）
```

### GP-009
```text
Title:               资产规模增长下的注册表/浏览器经济性
Category:            Content / Asset Workflow
Severity:            LOW（前瞻）
Game Context:        GP1-C 注册表已从 9 资产扩至 33（UI/FX/变体纹理）。
Observed Problem:    实测：33 资产下无检索/区分摩擦 —— 前缀命名约定
                     （ui_/fx_/proj_/prop_/banner_…）在清单与 ResolvePath
                     层面足够定位；headless 契约测试不经过浏览器点击流。
Current API Attempt: ContentRegistry 清单 + GUID（冻结契约内）。
Why Current API Is Insufficient: 暂无证据。
Workaround:          命名约定（前缀分类）。
Frequency:           每次资产入库。
Production Cost:     低（+1 清单行/资产）。
Cross-Game Evidence: 无。
Decision:            OBSERVE 维持 —— 编辑器点击流证据留待 GP1-D。
Level:               L0
```

### GP-008
```text
Title:               场景作者成本（30+ 实体时代的 JSON 经济性）
Category:            Content / Scene Authoring
Severity:            —（验证通过，结论性条目）
Game Context:        GP1-C 动态生成落地后实测：Main.scene 敌人条目 5 → 0，
                     场景仅剩 10 个布局对象（Player/Walls×4/Pads×4/Director），
                     运行时实体 ≥20 全部经 Engine.entity.spawn 产生。
Observed Problem:    不成立 —— 净效应为作者成本下降：场景行数减少，
                     运行时复杂度转移到 Lua 波次表（每敌边际 = +1 token）。
Current API Attempt: entity.spawn（冻结 API v2.1 内），引擎零改动。
Why Current API Is Insufficient: 不成立。
Workaround:          —
Frequency:           每次内容扩充。
Production Cost:     每敌边际 ≈ 1 token（任务 A 实测：+1 波次表项、0 场景行）。
Cross-Game Evidence: VS01（5 敌手写）→ GP01-C（0 条目）两局对比成立。
Decision:            结案（无摩擦）。静态摆场 vs 动态生成的选择权留给游戏作者，
                     引擎两种均已支持。
Level:               L0（结论性）
```

### GP-007
```text
Title:               内容重复率（波次编成中的结构性重复）
Category:            Content / Duplication
Severity:            —（验证通过，结论性条目）
Game Context:        GP1-C 五波编成共 32 个敌人 token（19G/6T/7S），
                     WAVES = { {...}, ... } 纯数据表。
Observed Problem:    不成立 —— "机械堆叠"正是编成的自然形态；
                     类型差异由 ENEMY_TYPES 数据表承载，波次差异由 token
                     序列承载，两层正交，无需"编成模板"抽象。
Current API Attempt: WAVES 纯数据表（Lua 内表达）。
Why Current API Is Insufficient: 不成立。
Workaround:          —
Frequency:           每波设计。
Production Cost:     每敌边际 1 token；新增类型 = +1 ENEMY_TYPES 行
                     +1 纹理资产 +1 清单行。
Cross-Game Evidence: 无。
Decision:            结案（无摩擦）。
Level:               L0（结论性）
```

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
Decision:            OBSERVE —— GP1-C Spawn 实验已完成复核（见下）：
                     账簿由 game.lua 自维护（Materialize 单点登记 +
                     E_NNN 计数器），生成点成本 ~10 行、存档编解码
                     各一段（Encode/Restore 合计 ~50 行），随持久化字段
                     线性增长且完全可控。17 项契约测试（含 ≥20 体动态
                     生成、波间存档冷启全等）全绿 —— 未出现"无法合理
                     表达"。find_all/tag 无晋升证据，DEFER 维持；
                     若未来敌人生成点多源化（多脚本各自记账）再复核。
Level:               L0→L1（已复现测量，缺口未成立）
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
| GP-DX-006 | Project Session 生命周期 | Editor/Session | LOW | L0 | OBSERVE（Human Run 实测） |
| GP-DX-005 | Viewport 仅 Billboard 反馈 | Editor/Rendering | LOW | L0 | OBSERVE（升级标准已定） |
| GP-DX-004 | Editor/Play 状态一致性语义 | Editor/State | MEDIUM(前瞻) | L0 | OBSERVE（Save=恒编辑态，Run3 裁决） |
| GP-DX-003 | Production UI Workflow Gap（人工验证 BLOCKED） | Editor/Production UI | BLOCKER* | L2 | PROMOTE → GP1-DX 最小 UI 闭环 |
| GP-DX-002 | 重名实体按名选择静默错选（DX05 二次复现） | Editor/Identity | MEDIUM | L1 | OBSERVE（真实内容事故才晋升） |
| GP-DX-001 | 跨工程上下文切换无自动恢复 | Workflow/Editor Context | LOW | L0 | OBSERVE（多场景工作流出现时复核） |
| GP-010 | Collision Event 再评价（手写圆碰撞规模成本） | Collision | — | L0 | DEFER 维持（结论性：晋升条件未出现） |
| GP-009 | 注册表/浏览器经济性（33 资产实测） | Content/Asset | LOW | L0 | OBSERVE（GP1-D 编辑器侧复核） |
| GP-008 | 场景作者成本（敌人条目 5→0 实测） | Content/Scene | — | L0 | 结案（无摩擦，成本下降） |
| GP-007 | 内容重复率（波次编成数据表） | Content/Dup | — | L0 | 结案（无摩擦） |
| GP-006 | 敌人查询依赖固定名册（Spawn 后复核） | Query | MEDIUM→收敛 | L1 | DEFER 维持（账簿自维护成本可控） |
| GP-005 | M003 Prefab 再评价（手写 5 实体成本） | Workflow | LOW | L0 | DEFERRED 维持（数据表已消除结构重复传播需求） |
| GP-004 | M002 再评价（战斗状态存续方案成立） | Serialization | — | L0 | DEFERRED 维持 |
| GP-003 | M004 再评价（is_down 边沿语义够用） | Input | — | L0 | DEFERRED 维持 |
| GP-002 | GP1-B 基线锁定声明 | Process | — | L0 | OBSERVE |
| GP-001 | 玩法代码多文件组织不可表达 | Workflow | LOW | L2(记录) | OBSERVE |

### GP-006 增补（GP1-C 实验记录）

动态生成 ≥20 体 + 五波推进 + 波间存档冷启全等，全部经 Lua 自维护账簿
完成（test_gp01 17/17）。测量结论：账簿成本 = 生成点 ~10 行 +
编解码 ~50 行，一次结构修改（改 ENEMY_TYPES 表）天然传播全实例 ——
该性质同时关闭了 M003 Prefab 的晋升触发条件。

### GP-001 增补（GP1-C 后）

game.lua 增长至 313 行（+波次机器/生成账簿/存档 v3），仍单文件可维护
（分区：ENEMY_TYPES/WAVES 数据契约 → Materialize/Spawn → Encode/Restore
→ AI/战斗 → 状态机）。晋升阈值维持 ~400 行或职责互相干扰时复核。
