�? GP01 Ledger �?真实生产摩擦账本

> 建立�?026-08-24（GP-P1 DAY 0）�?依据：`docs/GP-P1-Charter.md` §8
> 性质：GP-P1 期间一切引擎能力讨论的**唯一入口**。看到问�?�?直接�?Engine 是被禁止的�?
---

## 晋升规则（四级）

| Level | 名称 | 准入 | 动作 |
|-------|------|------|------|
| L0 | Friction | 发现问题 | 只登记，不改引擎 |
| L1 | Reproducible | 同一生产场景＋同一操作＋同一结果，稳定复�?| 可附最小复现案�?|
| L2 | Capability Gap | **硬闸�?*：证�?Engine API A �?API B �?Lua logic 无法合理表达 | 才允许进入实施流�?|
| L3 | Cross-game Evidence | 同类问题�?�? 局游戏复现 | 自动获得晋升资格 |

新能力实施流程（L2 通过后）�?
```text
GP Ledger �?Capability Proposal �?最�?API Contract �?Contract Test
�?Implementation �?Gameplay Revalidation �?Documentation �?Freeze / Defer
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
Production Cost:     # 定�?+ （可能的）定�?Cross-Game Evidence: # �?/ VS01(同类信号) / GP-xx ...
Decision:            # OBSERVE / PROMOTE / DEFER-UNTIL <条件>
Level:               # 当前到达的晋升级�?```

## 与历�?Ledger 的关�?
- DF01 缺口台账（M001–M006）：M001/M005 已实现并冻结；M002/M003/M004/M006 维持 ⏸，
  其再晋升条件�?`docs/GP-P1-Charter.md` §9 为准�?- VS01 缺陷账本（F1/F2/F-07）：F1 已修复并有回归守卫，不重复登记；
- 本账本只登记 **GP01 生产过程中新观察到的摩擦**�?
---

## GP-DX 条目区（GP1-D Editor Workflow�?026-08-25 起）

> 证据来源：`tests/test_gp01/GP01EditorWorkflowTest.cpp` 四契�?> （commit `222af42`，基�?121/121）。七元组口径�?> `docs/GP01-D-Editor-Workflow-Plan.md` §2�?
### GP-DX-001
```text
Title:               跨工程上下文切换无自动恢�?Category:            Workflow / Editor Context
Severity:            LOW（真实编辑器单场景不触发�?Game Context:        GP1-D RenameSafety：冷启验证工�?B 后返回工�?A 继续编辑
Observed Problem:    GameplayAPI 全局场景/输入绑定被新 ctx �?BeginFresh 抢占�?                     返回原工程后 HandleSpawn 静默落入他场�?—�?需手动三连
                     Reset/SetScene/SetInputProvider 重绑�?Current API Attempt: 手动三连重绑（可行但易忘；无"当前工程"概念）�?Why Current API Is Insufficient: 多工程并行时无自动恢复语义�?Workaround:          显式重绑三行（测试内已固化）�?Frequency:           每次多工程切换（headless 测试 / 未来多窗口）�?Production Cost:     单场景编辑器 = 0；多工程 = 每次切换 3 �?+ 遗忘风险�?Cross-Game Evidence: 无（DX02 同家族信号：全局绑定需手动维护）�?Decision:            OBSERVE —�?真实编辑器出现多场景/多窗口工作流时复核�?Level:               L0
```

### GP-DX-002
```text
Title:               重名实体按名选择静默错选（DX05 二次复现�?Category:            Editor / Entity Identity
Severity:            MEDIUM
Game Context:        GP1-D 同名探针：连续创建两�?"Crate" 并分别摆�?贴图
Observed Problem:    创建流程内按名定位命中首个同名旧实体 —�?第二只的
                     set_position/SetTexture 静默落到第一只身上；
                     存储层无损（索引对齐，双条目独立落盘），歧义纯在
                     选择层。测试改用插入索引定位绕开�?Current API Attempt: Scene::FindObject(name)（首中语义）/ 按名 idx 映射�?Why Current API Is Insufficient: name 是查询便利而非持久身份（GUID 才是），
                     重名时一切按名操作无二义性保证�?Workaround:          插入索引定位（创建流）；或保证命名唯一（纪律）�?Frequency:           每次重名实体的选择/绑定/查找�?Production Cost:     存储零损失；选择层每次重名即隐患，静默难排查�?Cross-Game Evidence: DX05（DF04 原始记录：SaveScene 绑定按名对齐歧义�?                     —�?两局同信号成立�?Decision:            OBSERVE �?L1（跨阶段复现达成）。仍不引�?Entity UUID
                     UX；晋升阈�?= 出现一次因重名导致的真实内容事故�?Level:               L0→L1
```

### DL-01 / DL-02 判决（GP1-D 正面实测�?
| �?| 判决 | 证据 |
|----|------|------|
| DL-01 活场景契�?| **CONFIRMED**：CaptureScene 存运行时位置（精�?teleport 落点），�?authored 值；漂移�?Save→冷启原样穿�?| IterationLoops It.2/It.3 断言 |
| DL-02 绑定表依�?| **CONFIRMED**：mid-session 新增实体未经绑定表同步，保存�?sprite/script GUID 静默�?Null；同�?push 后全程存�?| IterationLoops It.2 双向断言 |

编辑�?Save 语义因此完全可解释：**Save = 当前活场景定格快�?*
（含运行漂移），不是 authored 状态回滚。开发者心智模�?=
"PristineSave 先于战斗"；成本可控，暂无晋升动作�?

### GP-DX-003
```
Title:               Production UI Workflow Gap（人工生产验证阻塞）
Category:            Editor / Production UI
Severity:            BLOCKER（对 GP1-D Human Run 而言�?Game Context:        GP1-D 收尾裁决：自动化契约 4/4 GREEN，但人工完成 Arena v2
                     改造无法成�?—�?生产入口未闭环�?Observed Problem:    开发者无法仅�?Editor UI 完成 GP01 v1→v2 的修改与验证�?                     必须依赖内部 API / 手改文件。断点集中在接线层：
                     资产分配三条 DND 通路全断、ContentRegistry 与主编辑�?                     零连接、脚本编辑器为死代码、选择同步缺一环�?Current API Attempt: 全部生产能力已在冻结契约层被四契约证明（GoldenPath 等）�?                     缺的仅是 UI 入口 —�?Engine Capability 不是瓶颈�?Why Current API Is Insufficient: 不适用（API 充足，UI 层缺失）�?Workaround:          无（这正是阻塞本身）�?Frequency:           每次内容生产尝试�?Production Cost:     人工生产验证完全不可行�?Cross-Game Evidence: VS01（ScriptSandbox 单页形态已暴露同类需求）�?Decision:            PROMOTE —�?触发 GP1-DX / Production UI Foundation�?                     最�?UI 闭环（工程加�?内容分配/Hierarchy-Inspector-
                     Viewport 联动/Save/Play/Stop），目标 = 仅用 Editor 完成
                     GP01 v1→v2。不做完�?Unity/Unreal�?Level:               L2（晋升资格成立）
```

### GP-DX-004
```
Title:               Editor/Play 双场景状态一致性语�?Category:            Editor / State Model
Severity:            MEDIUM（前瞻）
Game Context:        GP1-DX Play = SceneSerializerV1 克隆 + 冻结 API 驱动�?                     编辑态与运行态并存�?Observed Problem:    待人工验证：Play 中修�?�?Stop �?Save，开发者预期与
                     实际保存物是否一致�?Current API Attempt: 设计语义已定：Save 恒存编辑态；Stop 整体丢弃克隆�?Why Current API Is Insufficient: 不适用（语义明确，风险在心智模型）�?Workaround:          �?Frequency:           每次 Play 后保存�?Production Cost:     若语义被误解则产�?改动丢失�?�?Cross-Game Evidence: 无（DL-01 同族：Save=活快照）�?Decision:            OBSERVE —�?GP1-D-Human-Run.md Run3 步骤6 实测裁决�?Level:               L0
```

### GP-DX-005
```
Title:               Viewport �?Billboard 反馈（Sprite 不可视）
Category:            Editor / Rendering Feedback
Severity:            LOW
Game Context:        GP1-DX P0 已知限制（计�?§3）�?Observed Problem:    无法在视口直接判断贴图绑定正确�?位置遮挡关系�?Current API Attempt: Content 面板 + Inspector 文案辅助判断�?Why Current API Is Insufficient: 待证�?—�?若人�?Run 判定受阻即升�?P1�?Workaround:          面板文案核对�?Frequency:           每次 Assign�?Production Cost:     �?中（取决�?Run2 实测）�?Cross-Game Evidence: 无�?Decision:            OBSERVE（升级标准见 GP1-D-Human-Run.md）�?Level:               L0
```

### GP-DX-006
```
Title:               Project Session 生命周期（重复加�?反复 Play�?Category:            Editor / Session
Severity:            LOW（前瞻）
Game Context:        Open Project 可重复触发；Play/Stop 可无限循环�?Observed Problem:    待实测：重复 Load 是否泄漏 Registry/Scene�?                     EventBus 是否重复注册；句柄是否累积�?Current API Attempt: 代码层预检干净 —�?Load 全量替换�?GameplayAPI::Reset�?                     Hierarchy.Init 仅启动一次�?Why Current API Is Insufficient: 不适用�?Workaround:          �?Frequency:           高频（每次重开工程/每局 Play）�?Production Cost:     泄漏则随会话时长劣化�?Cross-Game Evidence: GP-DX-001 同族（全局状态生命周期）�?Decision:            OBSERVE —�?Human Run 操作矩阵实测�?Level:               L0
```

### GP-DX-007
```
Title:               P0 假阳�?—�?UI 交付被人�?Run 即刻证伪
Category:            Process / Production UI
Severity:            HIGH
Game Context:        GP1-DX P0 交付�?c6ef22）后首次 Human Run：开发�?33 秒内
                     放弃使用；退出时 ASan heap-use-after-free�?Observed Problem:    �?视口仅图标无真实贴图，Assign 结果不可�?�?Run2 无法
                     人工验证；② ConsoleLog 环形缓冲先于 async 日志线程
                     析构，关闭即崩（ConsoleLog.cpp:28 use-after-free）�?Current API Attempt: P0 计划 §3 �?Billboard 呈现"登记为已知限�?—�?判定
                     错误：对"验证贴图是否绑对"这一核心任务而言这不�?                     可接受限制�?Why Current API Is Insufficient: 不适用（实�?验收问题，非能力缺口）�?Workaround:          �?Frequency:          每次 Human Run 必现�?Production Cost:     生产验证完全不可执行（GP1-D 继续阻塞）�?Cross-Game Evidence: 无�?Decision:            FIX NOW —�?�?Sprite 贴图进渲染路径；�?日志析构序修复；
                     教训入计划：P0 验收必须�?最小可视�?硬标准�?Level:               L1（复现于首次真实使用�?```
### GP-DX-008
```
Title:               UI 链路假成�?假删除簇（AUD-1..4，GP-DX-007 同构家族�?Category:            Process / Production UI
Severity:            HIGH
Game Context:        GP1-D Human Run 前置全量 UI 审计（docs/GP1-DX-UI-Audit.md�?                     2026-08-25，逐按钮链路追踪）
Observed Problem:    四处反馈层缺陷会直接污染人工 Run 判决�?                     �?Script Save 写盘失败仍打 "script saved"；② Play �?                     Assign Sprite 改动�?Stop 蒸发但报成功；③ 子对�?                     Delete 不解除父 children 引用（树/渲染仍见"已删"实体）；
                     �?Save Project 失败完全静默。审计同时暴�?                     GameObject::RemoveChild �?SetParent �?erase 的双�?                     迭代器失效——此前为零调用死代码，Scene 层修复首�?                     接线即触发（新契约测试当场捕获）�?Current API Attempt: 不适用（实�?验收问题，非能力缺口）�?Why Current API Is Insufficient: 不适用�?Workaround:          �?—�?Run 结果有效性直接受损（GP1-D 继续 BLOCKED）�?Frequency:           每次 Save / Assign / Delete 生产操作路径�?Production Cost:     人工验证判决不可信；假成功日志误导排障方向�?Cross-Game Evidence: GP-DX-007（同构教训："成功日志必须以真�?IO 结果为准"）�?Decision:            FIX NOW —�?AUD-1/2/4 反馈层补返回值校�?失败日志�?                     AUD-3 Scene::RemoveObject 父引用摘�?+ RemoveChild
                     迭代器序修正；契�?EditorWorkflow_DeleteSemantics(D7)
                     固化；基�?121�?22，gate GREEN。P1-b 清单见审计报�?§5�?Level:               L1（静态复�?+ 契约固化�?```
### GP-DX-010
```
Title:               编辑器无统一状态层（场�?选择/内容数据多点离散�?Category:            Editor / Architecture
Severity:            HIGH（结构性，前瞻�?Game Context:        GP1-D Human Run 前架构调查（用户观察触发；证�?=
                     docs/GP1-DX-State-Layer-Analysis.md 三路源码调查�?Observed Problem:    7 �?当前场景"持有点�? 个选择持有者（10 写入点）�?                     3 条互不相通的每帧通路（渲�?Update/UI 源）、双 PIE �?                     叠加三层场景；同步仅靠四路手工重�?lambda + 双通道冗余
                     EventBus；工程文档数据（Registry/Bindings/TexMgr/路径�?                     全部私活�?sandbox 会话头文件�?1 项已证实失效案例
                     （R1-R11），含错误落盘风险（R11）与 glfwTerminate 析构
                     未爆弹�?Current API Attempt: 不适用（架构组织问题，非单点能力缺口）�?Why Current API Is Insufficient: �?ProjectDocument/PlaybackController/
                     SelectionService 抽象——每个视图自持真相，无不变量守护�?Workaround:          手工重接 lambda（已知漏一路）+ 操作纪律（避免交叉序列）�?Frequency:           每次 Load/Play/Stop/New/Open/跨面板操作均处于风险面�?Production Cost:     状态不一致类 bug 难排查且随功能增长线性恶化；R1/R11 可致
                     数据事故�?Cross-Game Evidence: AUD-2/AUD-3（写错对象）、R2/R3（拾取错位）、DL-02（绑�?                     错位）均为同一根因的局部表现�?Decision:            OBSERVE —�?修复路线 §5 S1-S5 已排定；S1（双 PIE 合一 +
                     R7 单次广播 + Toolbar 非乐观态）�?S4（日志单缓冲 +
                     Undo 三线打通）已于 2026-08-25 执行落地，gate GREEN
                     122/122；剩�?S2/S3 建议 GP1-E 开工时 PROMOTE�?                     S5 涉序列化格式需 L2 评审�?Level:               L1（结构复�?+ 全量证据链固化；S1/S4 已收敛）
```
### GP-DX-011
```
Title:               图标码位表与真实字体脱节 + 运行期缩放假成功（UI 层级审计簇）
Category:            Process / Production UI
Severity:            MEDIUM
Game Context:        GP1-DX UI 层级专项审计（三准则：类别不�?/ 菜单不过�?/
                     字体显示完整位置正确；报�?docs/GP1-DX-UI-Audit.md §7�?Observed Problem:    �?自定�?ICON_FA_* �?70 宏中 4 码位不在 FA7 Solid 字体
                     cmap（Rotate 按钮 ×2 / SceneManager 标题 / 流式分组图标
                     渲染�?"?"）、ARROWS 字节错映射至警告三角 U+F071（移�?                     工具按钮图标语义错误）；�?UIManager.SetScale 运行�?                     no-op 但日志报 "Scale set to ..."（假成功）；�?主菜单栏
                     右侧版本文本窄窗口重叠菜单；�?View 菜单同一 Hierarchy
                     开关双入口（核心组 + Scene Panels 子菜单同 bool）；
                     �?Tools 菜单 ImGui 调试件与创作编辑器混类；
                     �?GP01 Production 单行混排工程 IO/播放/实体创建�?Current API Attempt: 不适用（资产表/实现缺陷，非能力缺口）�?Why Current API Is Insufficient: 不适用�?Workaround:          �?—�?图标错误/缩放失效直接可见可感�?Frequency:           每帧渲染（图标）/每次缩放操作/每次打开相关菜单�?Production Cost:     视觉语义误导排障成本；Human Run 录像可信度受损�?Cross-Game Evidence: GP-DX-007 / GP-DX-008 同构家族�?成功反馈必须以真�?                     结果为准"的字�?缩放版；假可供性再犯）�?Decision:            FIX NOW —�?5 码位�?cmap 复验替换�?0/70 PASS，双字体
                     文件一致）；RebuildFontAtlas 帧间重建 + ApplyEngineStyle
                     同步缩放�?.92 动态纹理自动重建）；版本文本宽度守卫；
                     View 双入口摘除；Tools 平铺分组不加深层级；Production
                     行三分组。gate ALL GREEN 122/122 + EditorDemo 冒烟通过�?                     OBS 登记：Toolbar W/E/R �?Play �?WASD 冲突（随 P1-c）；
                     Viewport 浮层宽距上限 / StatusBar 右簇预留硬编�?/
                     分隔符语言不统一（低危随 P1-c）。菜单深度实测最�?2 级，
                     合规，作为后续约束记录�?Level:               L1（机器比对静态复�?+ 构建级验证）
```

### GP-DX-011 增补（P1-c 第一批，2026-08-26�?
Toolbar 摘除手术 + Reset Layout 实装（审计报�?§8）：

- OBS-T1 闭环：W/E/R 全局键拦截随 Toolbar 死控件簇一并删除，Play �?  WASD 冲突源消除；
- 摘除裁决依据：桥�?= 为摆设扩 API 面；ViewportPanel 浮层已是 gizmo
  单一真相源（GP-DX-007"虚假可供�?纪律同脉）；
- 附带发现并修复：View>Reset Layout �?P0 �?DEAD（Init 期信号被读后
  丢弃）—�?删吞信号�?+ DockBuilder 四区规范布局实装�?- 连带死代码出清：DrawGizmo 第二套实现、InitDockingLayout 孤儿声明�?  Toolbar Reset 双入口�?
gate ALL GREEN 122/122；EditorDemo 冒烟两轮通过。P1-c 余项见审�?§8 尾注�?### Dogfood04 DX 台账复核（DX02/03/05/07�?
| 编号 | 复核结果 | 处置 |
|------|----------|------|
| DX02 输入手动绑定 | 不变 —�?�?headless 相关（编辑器宿主自动绑定）；GP-DX-001 为其多工程延�?| 维持 |
| DX03 导入新资�?| Import API 幂等自然（GoldenPath G2 直通）；文件对话框�?UI 层缺失，headless 无摩�?| 维持 |
| DX05 同名歧义 | **二次复现**（存储安全、选择层静默错选）�?�?GP-DX-002 | L1 |
| DX07 Import-before-attach | 本阶段未构造反序场景，未复�?| 留待 GP1-E |

---

## 条目�?
### GP-010
```text
Title:               Collision Event 再评价（手写圆碰撞的规模成本�?Category:            Collision
Severity:            —（验证通过，非摩擦�?Game Context:        GP1-C 33 实体同场：墙推挤/敌人接触伤害/攻击范围判定
                     三处碰撞逻辑（game.lua，dist() 圆检查合�?~20 行）�?Observed Problem:    无阻塞�?2 体每�?~32 次距离计�?+ 4 墙推出，
                     �?instruction 预算压力；碰撞代码占 game.lua ~6%�?Current API Attempt: Lua 手写圆碰撞（章程指定的故意测试项，非 workaround）�?Why Current API Is Insufficient: 不成�?—�?晋升条件（instruction budget
                     逼近上限或碰撞代码占比失控）均未出现�?Workaround:          �?Frequency:           每帧�?Production Cost:     新增一类碰�?�?+5~8 行（圆参�?+ 响应分支），线性可控�?Cross-Game Evidence: VS01 同模式（接触伤害/拾取均为圆检查）—�?两局同信�?                     且均无摩擦�?Decision:            DEFER 维持。复核条件：实体�?× 碰撞对帧成本进入
                     profiling 可见量级，或碰撞形状需求超出圆的表达范围�?Level:               L0（结论性证据条目）
```

### GP-009
```text
Title:               资产规模增长下的注册�?浏览器经济�?Category:            Content / Asset Workflow
Severity:            LOW（前瞻）
Game Context:        GP1-C 注册表已�?9 资产扩至 33（UI/FX/变体纹理）�?Observed Problem:    实测�?3 资产下无检�?区分摩擦 —�?前缀命名约定
                     （ui_/fx_/proj_/prop_/banner_…）在清单与 ResolvePath
                     层面足够定位；headless 契约测试不经过浏览器点击流�?Current API Attempt: ContentRegistry 清单 + GUID（冻结契约内）�?Why Current API Is Insufficient: 暂无证据�?Workaround:          命名约定（前缀分类）�?Frequency:           每次资产入库�?Production Cost:     低（+1 清单�?资产）�?Cross-Game Evidence: 无�?Decision:            OBSERVE 维持 —�?编辑器点击流证据留待 GP1-D�?Level:               L0
```

### GP-008
```text
Title:               场景作者成本（30+ 实体时代�?JSON 经济性）
Category:            Content / Scene Authoring
Severity:            —（验证通过，结论性条目）
Game Context:        GP1-C 动态生成落地后实测：Main.scene 敌人条目 5 �?0�?                     场景仅剩 10 个布局对象（Player/Walls×4/Pads×4/Director），
                     运行时实�?�?0 全部�?Engine.entity.spawn 产生�?Observed Problem:    不成�?—�?净效应为作者成本下降：场景行数减少�?                     运行时复杂度转移�?Lua 波次表（每敌边际 = +1 token）�?Current API Attempt: entity.spawn（冻�?API v2.1 内），引擎零改动�?Why Current API Is Insufficient: 不成立�?Workaround:          �?Frequency:           每次内容扩充�?Production Cost:     每敌边际 �?1 token（任�?A 实测�?1 波次表项�? 场景行）�?Cross-Game Evidence: VS01�? 敌手写）�?GP01-C�? 条目）两局对比成立�?Decision:            结案（无摩擦）。静态摆�?vs 动态生成的选择权留给游戏作者，
                     引擎两种均已支持�?Level:               L0（结论性）
```

### GP-007
```text
Title:               内容重复率（波次编成中的结构性重复）
Category:            Content / Duplication
Severity:            —（验证通过，结论性条目）
Game Context:        GP1-C 五波编成�?32 个敌�?token�?9G/6T/7S），
                     WAVES = { {...}, ... } 纯数据表�?Observed Problem:    不成�?—�?"机械堆叠"正是编成的自然形态；
                     类型差异�?ENEMY_TYPES 数据表承载，波次差异�?token
                     序列承载，两层正交，无需"编成模板"抽象�?Current API Attempt: WAVES 纯数据表（Lua 内表达）�?Why Current API Is Insufficient: 不成立�?Workaround:          �?Frequency:           每波设计�?Production Cost:     每敌边际 1 token；新增类�?= +1 ENEMY_TYPES �?                     +1 纹理资产 +1 清单行�?Cross-Game Evidence: 无�?Decision:            结案（无摩擦）�?Level:               L0（结论性）
```

### GP-006
```text
Title:               敌人查询依赖固定名册
Category:            Query
Severity:            MEDIUM（前瞻）
Game Context:        GP1-B 战斗循环：玩家攻击需找范围内敌人、导航需找最近活�?Observed Problem:    现有手段 = Engine.entity.find(名字) × 固定名册遍历�?                     场景实体静态时可用；一�?GP1-C 引入运行�?Spawn�?                     名册无法预知新实�?—�?查询链路断裂�?Current API Attempt: entity.find 逐名查询 + Lua 表维护（本阶段实际做法）�?Why Current API Is Insufficient: 动态生成场景下无解；静态规模下足够�?Workaround:          ENEMY_ROSTER 静态名�?+ ipairs 遍历距离排序�?Frequency:           每帧（攻击�?target + 导航）�?Production Cost:     当前 5 实体可接受；随实体数线性上升�?Cross-Game Evidence: VS01 同模式（固定 5 敌名册）—�?两局同信号，但触发条�?                     （动�?Spawn）尚未出现�?Decision:            OBSERVE —�?GP1-C Spawn 实验已完成复核（见下）：
                     账簿�?game.lua 自维护（Materialize 单点登记 +
                     E_NNN 计数器），生成点成本 ~10 行、存档编解码
                     各一段（Encode/Restore 合计 ~50 行），随持久化字�?                     线性增长且完全可控�?7 项契约测试（�?�?0 体动�?                     生成、波间存档冷启全等）全绿 —�?未出�?无法合理
                     表达"。find_all/tag 无晋升证据，DEFER 维持�?                     若未来敌人生成点多源化（多脚本各自记账）再复核�?Level:               L0→L1（已复现测量，缺口未成立�?```

### GP-005
```text
Title:               M003 Prefab 再评价（手写多类型实体的真实成本�?Category:            Workflow / Content
Severity:            LOW
Game Context:        GP1-B1 �?Main.scene 手写 Grunt×2/Tank×1/Scout×2
                     �?5 个敌�?JSON 条目（名�?GUID/坐标三行×5）�?Observed Problem:    无阻塞。重复成�?�?每敌 2 分钟、复�?GUID 时需小心
                     类型映射（grunt/tank/scout 各自 sprite GUID 不同）�?Current API Attempt: 直接手写场景 JSON（章�?§11 指定的故意测试项）�?Why Current API Is Insufficient: 不成�?—�?当前规模完全可接受�?Workaround:          �?Frequency:           一次性内容生产成本�?Production Cost:     低。若 GP1-C 动�?Spawn �?场景内摆实体"本身失效�?                     本条目性质将变化（由编辑成本转为生成机制问题）�?Cross-Game Evidence: VS01 五敌同模式�?Decision:            DEFERRED 维持（M003 不做）；GP1-C 复核�?Level:               L0
```

### GP-004
```text
Title:               M002 Component Serialization 再评价（战斗状态存续）
Category:            Serialization / Save
Severity:            —（验证通过，非摩擦�?Game Context:        GP1-B5 要求 Score/HP/敌我生死/掉血穿越 Save→Kill�?                     Restart→Load 全等恢复�?Observed Problem:    无阻塞。方�?= 场景序列�?位置/精灵) �?游戏自有
                     GameStateEncode/Restore 编码�?�?宿主�?_PERSIST
                     中介回灌。格式归 game.lua 所有，引擎零改动�?Current API Attempt: SceneSerializerV1 + ScriptInstance._PERSIST（冻结契约内）�?Why Current API Is Insufficient: 不成�?—�?RestartCombatState 证明全等恢复
                     （blobA==blobB）、死者仍死、可继续打到 Victory�?Workaround:          即上述编解码（它不是 workaround，是游戏代码的正常组成）�?Frequency:           每次 Save/Load�?Production Cost:     每类新增持久化字�?+1 行编�?+1 行解析（线性、可控）�?Cross-Game Evidence: VS01 boss �?_PERSIST 中介先例�?Decision:            DEFERRED 维持（M002 不做）。启动阈值维持章�?§9�?                     仅当数据必须�?Inspector/Scene 编辑时再议�?Level:               L0（结论性证据条目）
```

### GP-003
```text
Title:               M004 Input.pressed() 再评价（攻击边沿语义�?Category:            Input
Severity:            —（验证通过，非摩擦�?Game Context:        GP1-B3 战斗攻击需�?按一下打一�?/ 按住按冷却连�?�?Observed Problem:    无阻塞。is_down('J') + 冷却门控天然给出两种语义�?                     单帧按下=一刀；持续按�?cd 节拍连击
                     （CombatDamage 测试分别断言）�?Current API Attempt: Engine.input.is_down（冻�?API）�?Why Current API Is Insufficient: 不成立。若未来需�?精确按下次数计数"
                     （如连段），Lua �?prevHeld 边沿跟踪亦可表达�?Workaround:          �?Frequency:           每帧�?Production Cost:     零额外�?Cross-Game Evidence: VS01 menu RETURN 边沿同模式�?Decision:            DEFERRED 维持（M004 不做）�?Level:               L0（结论性证据条目）
```

### GP-002
```text
Title:               GP1-B 基线锁定声明
Category:            Process / Baseline
Severity:            —（非摩擦条目，证据锚点�?Game Context:        GP1-B Core Gameplay 开工前�?026-08-24�?Observed Problem:    �?—�?记录"开始前冻结 API 足以表达当前 gameplay"�?Current API Attempt: Scripting v2.1 / Content v1 / Lifecycle v1 / Editor v1 /
                     Physics v1.x / Save-Load 全部维持冻结�?Why Current API Is Insufficient: 尚无证据；本条目为后续任何新�?API 提供
                     证据起点�?Workaround:          �?Frequency:           一次性�?Production Cost:     �?Cross-Game Evidence: �?Decision:            OBSERVE
Level:               L0
```

### GP-001
```text
Title:               玩法代码多文件组织不可表�?Category:            Workflow / Scripting
Severity:            LOW（当前规模）
Game Context:        GP1-A 项目骨架搭建，章�?§6 规划 player/enemy/combat/
                     game/pickup 五个脚本模块
Observed Problem:    沙箱移除 dofile/loadfile/require（LuaEngine.cpp 沙箱
                     清单），场景仅支持单一 Director 脚本绑定；玩法代码只�?                     单文件增长�?Current API Attempt: 章程规划的多文件布局；RunFile 为宿主侧能力，Lua 内无
                     include 手段�?Why Current API Is Insufficient: �?—�?当前单文件仍然合理（VS01 arena
                     导演 199 行可维护）�?Workaround:          每场景单文件导演；模块化�?Lua �?local 表分区注释�?Frequency:           每次"想拆文件"时（结构性，非每帧）�?Production Cost:     随玩法规模线性上升；预计 GP1-B 加入三种敌人 AI 后开�?                     显现�?Cross-Game Evidence: VS01（同类约束下同样被迫单文件）—�?已构成两局同信号，
                     但尚无生产成本证据支撑晋升�?Decision:            OBSERVE
Level:               L0→L2 已记录（缺口确凿），晋升等成本证�?```

### 裁定汇总表

| 编号 | Title | Category | Severity | Level | Decision |
|------|-------|----------|----------|-------|----------|
| GP-DX-011 | 图标码位脱节 + 缩放假成功（UI 层级审计簇） | Process/Production UI | MEDIUM | L1 | FIX NOW（已修，70/70 cmap 复验 + gate 122 GREEN�?|
| GP-DX-010 | 编辑器无统一状态层（多点离散） | Editor/Architecture | HIGH(结构) | L1 | OBSERVE（S1-S5 路线已排定，GP1-E 或事故触�?PROMOTE�?|
| GP-DX-008 | UI 链路假成�?假删除簇（AUD-1..4�?| Process/Production UI | HIGH | L1 | FIX NOW（已修，D7 契约固化�?22 GREEN�?|
| GP-DX-006 | Project Session 生命周期 | Editor/Session | LOW | L0 | OBSERVE（Human Run 实测�?|
| GP-DX-005 | Viewport �?Billboard 反馈 | Editor/Rendering | LOW | L0 | OBSERVE（升级标准已定） |
| GP-DX-004 | Editor/Play 状态一致性语�?| Editor/State | MEDIUM(前瞻) | L0 | OBSERVE（Save=恒编辑态，Run3 裁决�?|
| GP-DX-003 | Production UI Workflow Gap（人工验�?BLOCKED�?| Editor/Production UI | BLOCKER* | L2 | PROMOTE �?GP1-DX 最�?UI 闭环 |
| GP-DX-002 | 重名实体按名选择静默错选（DX05 二次复现�?| Editor/Identity | MEDIUM | L1 | OBSERVE（真实内容事故才晋升�?|
| GP-DX-001 | 跨工程上下文切换无自动恢�?| Workflow/Editor Context | LOW | L0 | OBSERVE（多场景工作流出现时复核�?|
| GP-010 | Collision Event 再评价（手写圆碰撞规模成本） | Collision | �?| L0 | DEFER 维持（结论性：晋升条件未出现） |
| GP-009 | 注册�?浏览器经济性（33 资产实测�?| Content/Asset | LOW | L0 | OBSERVE（GP1-D 编辑器侧复核�?|
| GP-008 | 场景作者成本（敌人条目 5�? 实测�?| Content/Scene | �?| L0 | 结案（无摩擦，成本下降） |
| GP-007 | 内容重复率（波次编成数据表） | Content/Dup | �?| L0 | 结案（无摩擦�?|
| GP-006 | 敌人查询依赖固定名册（Spawn 后复核） | Query | MEDIUM→收�?| L1 | DEFER 维持（账簿自维护成本可控�?|
| GP-005 | M003 Prefab 再评价（手写 5 实体成本�?| Workflow | LOW | L0 | DEFERRED 维持（数据表已消除结构重复传播需求） |
| GP-004 | M002 再评价（战斗状态存续方案成立） | Serialization | �?| L0 | DEFERRED 维持 |
| GP-003 | M004 再评价（is_down 边沿语义够用�?| Input | �?| L0 | DEFERRED 维持 |
| GP-002 | GP1-B 基线锁定声明 | Process | �?| L0 | OBSERVE |
| GP-001 | 玩法代码多文件组织不可表�?| Workflow | LOW | L2(记录) | OBSERVE |

### GP-006 增补（GP1-C 实验记录�?
动态生�?�?0 �?+ 五波推进 + 波间存档冷启全等，全部经 Lua 自维护账�?完成（test_gp01 17/17）。测量结论：账簿成本 = 生成�?~10 �?+
编解�?~50 行，一次结构修改（�?ENEMY_TYPES 表）天然传播全实�?—�?该性质同时关闭�?M003 Prefab 的晋升触发条件�?
### GP-001 增补（GP1-C 后）

game.lua 增长�?313 行（+波次机器/生成账簿/存档 v3），仍单文件可维�?（分区：ENEMY_TYPES/WAVES 数据契约 �?Materialize/Spawn �?Encode/Restore
�?AI/战斗 �?状态机）。晋升阈值维�?~400 行或职责互相干扰时复核�?

---

## AV ϵ�У�Avalonia Editor Migration��2026-08-26 ���ˣ�

> ���ݣ�docs/Avalonia-Editor-Migration-v1.md��branch: avalonia @ 6ed3a45����
> ԭ��Engine contracts frozen ���� Ǩ��ֻ�滻 Editor Presentation + Interaction Layer��

### AV-001 Avalonia Architecture
Title: UI framework migration requires a stable EditorSession boundary
Category: Architecture/Editor
Severity: HIGH
Observed Problem: ImGui �����༭ҵ���߼������ EngineEditor/GP01ProductionSession �Ļ���·���У��޷����ڶ��� frontend ����
Decision: PROMOTED��2026-08-26 Phase 0 Gate ALL GREEN������ĩ������
Level: L2(��¼)

### AV-002 In-Process Host
Decision: DECIDED ���� Avalonia v1 ����ͬ���� C ABI bridge��IPC ����Ϊ v1 ǰ�������������ģ�ͱ�֤δ���ɸ���Ϊ��������
Level: �þ�

### AV-003 Viewport
Decision: DECIDED ���� Texture Presentation ΪĿ��ܹ���Renderer��Texture��Avalonia����NativeControlHost ������Ϊ Phase 7 ǰ����ʱ adapter��RenderGraph SG6 FAIL/ISOLATED ״̬���ý�Ǩ���ؿ�
Level: �þ�

### AV-004 Dual UI
Decision: DECIDED ���� ˫ UI ���ڹ��� + Panel-by-Panel + parity gate + ����ɾ�� ImGui������ UI ������״̬������EditorSession ��Ψһ��ʵ״̬
Level: �þ�

### AV-005 EditorSession
Decision: P0 ARCHITECTURAL REQUIREMENT ���� �����������κ����Ǩ�ƽ�����bridge/editor_bridge��C ABI + ��ͷ�Ự���ģ������� UI ���� Session ��������
Level: P0


### AV-001 ������Phase 0 Spike ִ�м�¼��2026-08-26��

AV-001/AV-005 ��ʵʩ���������ţ�branch: avalonia����

```text
bridge/editor_bridge��C ABI DLL�������㣩
  EditorSession_Create/Destroy
  OpenProject / SaveProject / GetEntityCount / GetAssetCount
  CreateEntity���Զ�������/ GetEntityName
  SetEventCallback��C++��C# ͬ���¼���EV_PROJECT_LOADED=1 / EV_ENTITY_CREATED=2��
  GetLastError������ͨ����

editor_avalonia��Avalonia 11.2.7 + net9.0 ������P/Invoke �� Native/EditorBridgeApi.cs��
```

Evidence��

- ��Լ���� tests/test_bridge/test_editor_capi.cpp ���� **6/6 PASS**
  ������������� / NullHandle ��Լ / ���̴� round-trip /
  ʵ�崴��+�¼� / ����ͨ�� / δ�����̱���ܾ���
- Phase 0 ���� `AvaloniaEditor --selftest` ���� **ALL GREEN (4.9s)**��
  Avalonia ���� �� P/Invoke �� C ABI �� C++ �� Engine��
  scratch ���� Open(10 objects/33 assets) �� CreateEntity(Entity) ��
  Save(11 entities) �� �ؿ��Ự��֤�־û�(11 objects)��
- integrity_gate ALL GREEN 122/122���������Ķ���ع飩

������ע����ͷ����ͬ����Ҫ `ASAN_WIN_CONTINUE_ON_INTERCEPTION_FAILURE=1`
���¾� ASan Ԫ���ݶ������⣬�� UI-Audit ��8.1 INV ϵ���� Human-Run Э�飩��

�þ���AV-001 **PROMOTED**��Phase 0 ֤����������ê�� avalonia-phase0-av001����Phase 1 Editor Shell ������


### AV-001 ������Phase 1 P1-A/B/C ִ�м�¼��2026-08-26��

Shell �Ǽ�������������Լ��أ�commit �� 3e17c11��ê�� avalonia-phase0-av001����

- **P1-A Shell**��Menu(File) / Toolbar(Open��Save��+Entity) / �� Hierarchy /
  �� Viewport ռλ(Texture presentation �� Phase 7) / �� Inspector(Transform) /
  �� Console��AssetBrowser Tab / StatusBar��Avalonia ԭ�� Grid ���֣�
  Docking ����Dock.Avalonia����������롣
- **P1-B ��������**��Open �� EV_PROJECT_LOADED �� VM ���ؼ��� �� Pane ��ˢ�£�
  EntityCreated ��ͬһ�¼�·����Pane ��������ã���Save/�ؿ�һ�¡�
- **P1-C ��������**��View �� MainViewModel(INPC) �� EditorHostService(Session ABI)
  �� Engine��C# ���������ڲ�����й©��
- **ABI ��չ**������������Լ���ѣ����� Engine API����
  Get/SetEntityPosition(px/py/pz)��GetAssetPath/GetAssetType��EV_ENTITY_MOVED��
  SetEntityPosition ���ñ༭̬����������GP-DX-004 ���壬Play ̬д��ܾ���
  m_Playing ռλ�� Phase 7 Runtime track ���ߣ���

Evidence ���� Golden Gate 1��`--gate1`�����ظ�����**ALL GREEN 3.35s**

```text
open GP01(scratch)      -> hierarchy 10 entities / assets 33   PASS
select Player           -> inspector name + transform ��ʾ      PASS
edit PosZ 4 -> 9 Apply  -> EV_ENTITY_MOVED + inspector ��ӳ     PASS
Save                    -> д�̳ɹ�                             PASS
reopen session          -> persisted Player.z == 9.0            PASS
```

�ع飺test_bridge 8/8���� TransformRoundTrip / AssetQueryContract ������������

---

## Phase 1 FROZEN / Phase 2 Editor Core (2026-08-28)

### P2-0 Ruling
Phase 1 (Editor Shell + Session lifecycle + one-way dependency + Golden Gate 1) is FROZEN.
Evidence sealed at commit c16b327: test_bridge 8/8 PASS + AvaloniaEditor --gate1 ALL GREEN
(open GP01 -> 10 objs / 33 assets -> select Player -> inspector transform -> edit Z 4->9 ->
Apply -> Save -> reopen -> persisted z==9.0).

### Phase 2 scope (Editor Core / State Architecture)
P2-A EditorState unified model, P2-B minimal Event Contract,
P2-C Hierarchy <-> Inspector real linkage (select -> inspect -> edit -> selection kept),
P2-C2 Rename/Delete/Create loop, P2-D Asset Browser VM (search/filter/guid/path/assign),
P2-E Dirty state (Clean->Edit->Dirty->Save->Clean) + unsaved-close prompt (no Undo/Redo,
per DF08: not P0),
P2-F Script Editor VM (select game.lua -> load -> edit -> dirty -> save) preserving _PERSIST semantics.
AV-G2 Editor Workflow Golden Gate: full cold-start -> open -> select -> edit -> rename ->
assign sprite -> create -> select new -> edit -> script edit -> save -> close -> reopen ->
verify all state. Acceptance: C++/Engine 0 change, JSON 0 manual edit, ImGui 0,
Avalonia->C ABI->EditorSession sole path, AV-G2 PASS, integrity gate GREEN.

No-Capability-Without-Evidence discipline applies: no new ABI method without a real UI/VM
blocking need + contract test.

### P2-1 ABI batch (2026-08-28) — verified

Phase 2 ABI layer implemented + contract-tested (uncommitted, working tree):

- **P2-C2 实体回路**: `EditorSession_DeleteEntity` / `EditorSession_RenameEntity`
  (empty/whitespace name rejected, duplicate name rejected case-sensitive, self-rename ok),
  both edit-state-only (playing 拒绝), broadcast EV_ENTITY_DELETED / EV_ENTITY_RENAMED.
- **P2-D 资产前驱**: `EditorSession_GetAssetGuid` (32-hex), `EditorSession_GetEntitySprite`
  (resolve via binding GUID), `EditorSession_AssignSprite` (texture-only asset type check;
  DL-02 纪律路径: AddComponent 或 SetTexture + binding writeback; idempotent re-assign;
  EV_ENTITY_ASSIGNED).
- **P2-F 脚本前驱**: `EditorSession_ScriptRead` / `EditorSession_ScriptSave` (真实写盘结果
  为准，AUD-1 纪律：写失败即失败，不报假成功；相对路径锚定 CWD 与 GP01 运行时一致)。
- **P2-E 脏状态**: `EditorSession_IsDirty` — Create/Delete/Rename/SetPosition/Assign
  置位，Save 清位；OpenProject 复位。Save 成功广播 EV_PROJECT_SAVED。

Evidence: **test_bridge 13/13 PASS**（新增 5 契约：DeleteEntityAndEvent /
RenameEntityRules / AssignSpriteTwiceIdempotentTexture / ScriptReadWriteViaRegistry /
DirtyStateContract；写类用例走 `gp01_editor_scratch/gate_p2` 隔离副本 + CWD 重定向），
**integrity gate ALL GREEN 122/122**。C++/Engine 零改动（bridge 层内），Avalonia 侧
VM/View 接线未动——P2-C/D/F 的 UI 消费方待下一批。

**ASan 实抓 1 个真 bug（bridge/src/EditorSession.cpp）**: `ContentRegistry::GetAllEntries()`
按值返回临时 vector，`const auto& entry = m_Reg.GetAllEntries()[i]` 绑到临时元素上，
离开语句即悬垂 —— AssignSprite/ScriptRead/ScriptSave 三处同构，AssignSprite 首次
合约测试即触发 heap-use-after-free。修复：按值拷贝 entry。教训登记：**按值返回容器的
临时元素不可持有引用**，已修 + ASan 回归在跑（test_bridge 全程 ASan 下通过）。
测试侧另修 3 处：EXPECT_EQ 双实参求值顺序未定义（先取回再断言）、scratch CWD 切换后
路径须用相对 assets/gp01、game.lua 12.6KB 需 16KB 缓冲（cap-1 截断语义）。

### P2-2 VM/View batch (2026-08-28) — AV-G2 PASS

Avalonia 侧消费新 ABI，全程 ViewModel → Session ABI → Engine 单向路径：

- **EditorBridgeApi.cs**: 新增 8 个 P/Invoke + EV_PROJECT_SAVED/EV_ENTITY_DELETED/
  EV_ENTITY_RENAMED/EV_ENTITY_ASSIGNED 事件常量。
- **EditorHostService.cs**: DeleteEntity/RenameEntity/GetEntitySprite/AssignSprite/
  GetAssetGuid/ScriptRead(两段式：先 probe 长度再全量读)/ScriptSave/IsDirty 包装。
- **MainViewModel.cs**: P2-C2 Create/Rename/Delete 命令（Rename 空名拒绝透传）、
  P2-D AssignSpriteToSelected、P2-F OpenScript/SaveScript（ScriptDirty 标记）、
  P2-E IsDirty（编辑置位/Save 清位）+ ResetSession；ReloadCollectionsFromSession
  重载后按索引保持选择（P2-C"selection kept"纪律）。
- **MainWindow.axaml**: Hierarchy 内嵌 Rename 输入框+Delete/+Entity、Inspector
  Sprite 只读行、Asset Browser Assign 按钮、Script Editor Tab（Consolas 编辑框
  + 脏标记）、StatusBar DIRTY 徽标、File>Close（未保存弹 ConfirmDialog）。
  ConfirmDialog 为纯代码零依赖实现（Avalonia 11.2.7 无内置 MessageBox，
  不为此引入 NuGet 包）。
- **Phase2Gate.cs** (`--gate2`)：AV-G2 全链路 34 断言 —— open(10/33) → select
  → edit transform(z 4→9) → rename(Player→Hero, selection kept) → assign sprite
  → create NewProp → edit transform → script edit(append+save) → save project
  → reopen → 11 entities / Hero.z==9.0 / sprite binding / NewProp transform /
  script audit line 全部持久化。CWD 定向 scratch 工程根（与 C++ 契约测试同语义，
  manifest 相对路径按 GP01 运行时 CWD 解析）。

Evidence: **AV-G2 ALL GREEN (2.1s)** + **gate1 回归 GREEN** + **test_bridge 13/13**
+ **integrity gate 122/122 ALL GREEN**。C++/Engine 零改动；ImGui 零；JSON 零手改。

Phase 2 已冻结（commit b2919c9，tag `avalonia-phase2-av-g2`），
Closure 见 `docs/Avalonia-Phase2-Closure.md`。

### P3-0 Charter + P3-A Hierarchy (2026-08-28)

- **Phase 3 Charter**（`docs/Avalonia-Phase3-Charter.md`）：Production Panels
  目标 = Avalonia 承担完整日常内容生产流程；P3-A..F + AV-G3；晋升规则 AV-GP-xxx；
  Viewport 红线（Phase 7 隔离）；不新增 Engine API 仅为 UI 完整。
- **P3-A Hierarchy 正式版**：Search 过滤（大小写不敏感子串）+ 选择状态契约。
  - VM 层 FilteredEntities 过滤视图，主列表 Entities 为会话事实源；
  - **选择状态契约**：过滤把选中项藏掉时模型层保留选择，清空过滤后自动恢复；
    重命名/重载后按索引保持选择（P2-C 纪律延续）；
  - View 层 ListBox 改 OneWay + SelectionChanged 上抛（修复 TwoWay 回写 null
    覆盖 VM 选择的契约冲突 —— AV-004 单一事实源在 View 边界落地）。
- **Phase3Gate.cs**（`--gate3a`）：30 断言 —— open(10) → search 'Pad'→4 →
  过滤藏选→清空恢复 → create under filter → rename（过滤实时跟随）→ delete
  （选择清空）→ save → reload 全恢复（10 entities / filter 重验 / selection kept）。

Evidence: **P3-A GATE ALL GREEN (2.1s)** + gate1/gate2 回归 GREEN +
test_bridge 13/13 + integrity gate 122/122 ALL GREEN。C++/Engine 零改动。
