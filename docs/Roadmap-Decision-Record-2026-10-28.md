# 路线裁决记录（2026-10-28）

> **状态**：FROZEN（Stage 0 交付；不授权实现）
> **范围**：D-1 F2 权威映射 · D-2 Content v2 M1-0 四项契约 · D-3 编辑器生产主线
> **上游**：`docs/Engine-Foundation-Phase-Plan.md`、`docs/Engine-Foundation-F2-Charter.md`、
> `docs/Engine-Foundation-F2B-Closure.md`、`docs/Content-v2-M1-0-Source-Audit-Contract.md`、
> `docs/Asset-Pipeline-v2-Report.md`
> **纪律**：本记录只做路线裁决与契约冻结，不含实现代码；M1-A 起才允许改生产代码。

---

## 0 · 本记录解决什么

同一仓库里存在三条互相冲突的路线表述。若不在开工前消除，后续每个阶段都会重新解释
一次「F2-C 是什么」「M1-A 是什么」「哪个编辑器算生产」，最终让证据无法对账。

| 编号 | 冲突 | 本记录的裁决 |
|---|---|---|
| D-1 | F2 阶段编号有三套写法 | `Engine-Foundation-Phase-Plan.md` 为 F0–F5 的权威编号；**F2-C = RigidBody Contract**；Collider Serialization 另立 `F2-S1`，阶段归属刻意留空 |
| D-2 | Content v2 M1-0 仍是 DRAFT，四项契约未冻结，M1-A~F 顺序与依赖倒置 | 冻结四项契约 K1–K4；M1 字母重新绑定为 K4 → K1 → K2 → K1 完整化 → 导入汇点 → 全链门禁 |
| D-3 | `editor_avalonia` 与 Nova 两套编辑器，生产主线未成文 | **`editor_avalonia` 为唯一生产编辑器主线**；Nova 保持原型；不并行维护；未来迁移须单独提案 |

本记录是上述三条路线的**单一解释源**。冲突的原文不删改；两份主要冲突文档各加一条
errata 注记指向本记录（见 §2.3、§3.9）。

---

## 1 · 证据边界

- 本轮核对：HEAD `ee88bc8`（`avalonia`）。工作树仅有三个子模块脏标记
  （`third_party/d3d12ma`、`third_party/openal-soft`、`third_party/vma`）与未跟踪
  `crashes/`；均不在本阶段范围，未触碰。
- 本轮**未构建、未运行测试、未执行任何 Git 写操作**（无 commit / push / tag / merge）。
- D-1 / D-2 的事实源是文档原文（行号见各节；行号为 2026-10-28 errata 注记后的当前
  文件，M1 实施时须复核）与 2026-09-16 M1-0 的源码静态审计；本轮未重新复现故障。
- D-3 的事实源是上一轮代码清点与本轮定点核对：
  `editor_avalonia/Native/EditorBridgeApi.cs`、`editor_avalonia/Services/EditorHostService.cs`、
  `editor_avalonia/Services/ProductionPathGate.cs` 表明 bridge/导入/生产路径已接线；
  `src/Nova.Editor.Services/Model/SampleSceneFactory.cs`、
  `src/Nova.Editor.Services/Viewport/PlaceholderViewportHost.cs` 表明 Nova 仍是样例/占位视口。
  本轮未重新运行桥接或 UI 验收。
- **不冻结任何测试计数。** `Content-v2-M1-0` 的 212 是 F2-B 冻结时的 in-scope 数；
  `docs/Evidence-Baseline.md` 当前表内合计（240）与逐行之和（251）不一致，且未覆盖
  全部 CI target。M1-A 开工前必须以实际 gtest discovery / Integrity Gate 重新对账；
  新增用例按实际运行结果入基线。本轮无构建，故无警告计数可记。
- 既有未判定失败、D9-F 封存数字、CI 历史记录均保持原状；本记录不引用任何历史
  通过数量作为本阶段证据。

---

## 2 · D-1 — F2 权威映射

### 2.1 冲突事实

| 来源 | 表述 | 冲突点 |
|---|---|---|
| `Engine-Foundation-Phase-Plan.md:11` | 「下一目标是 **F2-C RigidBody Contract**（Contract-only，不做 Adapter）」 | F2-C = RigidBody |
| `Engine-Foundation-F2B-Closure.md:123-124` | 「后续 **RigidBody（F2-C）** 将以本 tag 为基线」 | 与 Phase-Plan 一致 |
| `Engine-Foundation-F2-Charter.md:33-36` | F2-C = Serialization；F2-D = Lua；F2-E = Avalonia；F2-F = 真实互操作 | F2-C 与权威来源直接冲突 |
| `Engine-Foundation-F2-Charter.md:76` | 「❌ RigidBody」列入「F2 特别不做什么」 | 与 F2-C = RigidBody Contract 表面冲突 |
| `Content-v2-M1-0-Source-Audit-Contract.md:105` | 「F2-C RigidBody、F2-D Physics Binding」 | Physics Binding 实际是已冻结的 **F2-B** |

F2-B = Physics Binding 由 `F2-Charter.md:32` 与 `F2B-Closure.md` 全链证据共同确定；
`Content-v2-M1-0:105` 的「F2-D Physics Binding」是错误标签，不是新阶段。

### 2.2 冻结映射

**权威源**：`docs/Engine-Foundation-Phase-Plan.md`。F0–F5 的编号以它为准；其他文档的
F2-* 写法只在本记录 §2.2 的映射表内有效。

| 编号 | 权威含义 | 状态 | 说明 |
|---|---|---|---|
| F2-A | Collider Contract | ✅ FROZEN | tag `engine-foundation-v1-f2-a-collider`（`f674331`） |
| F2-B | Physics Binding | ✅ FROZEN | tag `engine-foundation-v1-f2-b-collider`；F2-B Closure 记录 in-scope 212 |
| **F2-C** | **RigidBody Contract** | ◀ NEXT | **Contract-only，不做 Adapter**；执行体是 T1-A（规范 + GameObject 契约 + 与 ECS `RigidBody3DComponent` 的边界），不含 T1-B~F |
| F2-D | 未分配 | — | 不得用于 Physics Binding（那是 F2-B）、不得用于 Lua（见 §2.3） |
| F2-E / F2-F | 未在 Phase-Plan 定义 | — | 不得继续作为阶段编号使用；Charter 的对应内容按 §2.3 映射到 F4 / F5 |

**F2-S1 — Collider Serialization（独立交付项，不是阶段字母）**

- 承接 `F2-Charter.md:33` 原 F2-C 行的内容：Create → Save → Cold Restart → Load →
  Physics representation 真正还原。
- 编号 `F2-S1` 是独立交付键，**不继承 F2-C**，也不自动落入 F3。
- 其阶段归属**刻意留空**，直到 F2→F3 映射冻结；当前不得把 `F2-S1` 当成 F3 的既定交付。
- F2-B 的 B5/B6 已提供 Collider 的 Cold Restart / 行为一致性证据；`F2-S1` 开工前先做
  gap analysis，已由 F2-B 覆盖的部分不重复计为新增证据。
- 之所以不直接并入 F3：F3 是「Serialization + Runtime Lifecycle」的阶段级推广，
  `F2-S1` 是 Collider 单组件的既留交付；两者粒度不同，合并会重演本次编号冲突。

### 2.3 F2 Charter errata（原文保留）

`docs/Engine-Foundation-F2-Charter.md` 的正文不删改；在其头部加 errata 注记后，按以下
表格读取。

| Charter 原文 | 原文含义 | 2026-10-28 后的权威读法 |
|---|---|---|
| §2 `F2-C Serialization` | Collider Save/Cold Restart/Load/Physics 还原 | 独立交付项 **F2-S1**；不是 F2-C；阶段归属待定 |
| §2 `F2-D Lua` | 只暴露通用 `Engine.component.*` | 不是阶段字母；是 F2「每个组件可经脚本访问」的验收要求，RigidBody 侧由 **T1-D** 承载 |
| §2 `F2-E Avalonia` | Inspector 查看/修改/Add/Remove/Dirty/Save 且进入 Physics | 映射到权威 **F4 Avalonia Component Authoring**；生产目标是 `editor_avalonia` |
| §2 `F2-F 真实互操作` | Collider + Physics 端到端 Cold Restart | 映射到权威 **F5 Foundation Golden Gate** 的跨组件证据；不是 F2 字母 |
| §4 `❌ RigidBody` | F2 特别不做什么 | 视为 **F2-A/B 的范围栅栏**，不是阶段级永久禁令；F2-C 只做 RigidBody Contract，Adapter 留给 T1-C，栅栏相应收窄为「F2-C 不做 Adapter」 |
| §7 `F2-C 往返测试` | SceneSerializerV1 的 Collider 往返 | 历史标签；Collider 序列化归 **F2-S1**；RigidBody 序列化归 **T1-B** |
| 头部「下一步：F2-B」 | 2026-08-28 的历史状态 | F2-B 已由 `F2B-Closure.md` 冻结；当前下一步是 F2-C |

`Engine-Foundation-F2-Closure.md`、`Engine-Foundation-F2B-Plan.md` 中的 F2-E / F2-F
引用按上表读取为 F4 / F5；它们作为历史验收记录保留，不回改。

### 2.4 Content M1-0 第 100 行的更正

`Content-v2-M1-0-Source-Audit-Contract.md:105` 的「F2-C RigidBody、F2-D Physics Binding」
按以下方式读取：

- F2-C = RigidBody Contract（正确，保留）；
- 「F2-D Physics Binding」= **F2-B Physics Binding（已冻结）** 的错误标签；
  未来与 RigidBody 相关的物理绑定属于 **T1-C**，不是 F2-D；
- 该行的边界结论不变：Engine Foundation 与 Content Foundation 并行、互不引入依赖；
  汇合点仍是「组件引用的资产必须来自稳定 Content Identity」。

### 2.5 F2-C 的边界

F2-C 只交付 RigidBody 的**契约**：

- 稳定类型名、字段集、反射元数据、Registry factory；
- 与 F1 的单一 GameObject/Component 契约一致；
- 与 ECS `RigidBody3DComponent` 的边界写成规范，但**不实现 ECS 桥接**；
- 不写 Physics Adapter；不改序列化；不改 Lua；不改 Inspector。

T1-B（序列化 + 冷启恢复）、T1-C（真实物理绑定）、T1-D（Lua v2.1）、T1-E（Inspector）
是 F2-C 之后的独立交付，不得被写成 F2-C 的完成条件。

### 2.6 刻意留空

- `F2-S1` 的阶段位置未定；只有 F2→F3 映射冻结后才能定。
- F2-D 未分配；Prefab / Physics Material 仍是 Phase-Plan §2 F2 的第 2/3 项内容，
  但未获字母编号，不得自行占用 F2-D。

---

## 3 · D-2 — Content v2 M1-0 四项契约

### 3.1 契约地位

`Content-v2-M1-0-Source-Audit-Contract.md` 保留为**审计原文**：S01–S09 的源码事实与
C01–C09 的裁决继续有效，但以下内容由本记录冻结或取代：

- 四项契约 K1–K4（§3.2–§3.5）；
- M1-A~F 的顺序与字母含义（§3.6）；
- §8 开放问题 1–4 的裁决（§3.10）；
- §9 的「M1-0 完成条件」与「下一步 M1-A Manifest...」的旧表述。

**旧引用必须重绑定。** 2026-10-28 之前所有 M1-A~F 的引用按下表读取；新工单只能使用
§3.6 的新表。

| 旧引用 | 旧含义 | 新编号 |
|---|---|---|
| §5 C05「M1-B 的 rename」 | Rename 事务 | **M1-C** |
| §6 M1-A | Manifest 严格加载 | **M1-B** |
| §6 M1-B | Rename 事务 | **M1-C** |
| §6 M1-C | 工程快照提交 | **M1-D** |
| §6 M1-D | ProjectRoot + 路径迁移 | **M1-A** |
| §6 M1-E | 导入服务 | **M1-E**（含义不变） |
| §6 M1-F | Failure-Injection Gate | **M1-F**（范围扩展为全链） |
| §8 Q1「M1-C 设计期」 | commit 指针形态 | **M1-D** |
| §8 Q2「M1-B 设计期」 | rename 旧源保留策略 | **M1-C** |
| §8 Q4「M1-D 前」 | 大小写路径策略 | **M1-A 前** |
| §9「之后进入 M1-A Manifest...」 | 下一步 | 旧文；新顺序是 **M1-A ProjectRoot** |

### 3.2 K1 — 单一提交指针 + 单调 Revision

**K1.1** 工程提交的最小单位是一个不可变 revision：Manifest、Scene 与提交元数据在同一个
`Revision` 下准备、校验、发布。

**K1.2** `current` 是唯一提交点。读取端只认 `current`；禁止扫描目录取最大 revision；
禁止把未提交候选升级为 `current`。

**K1.3** `Revision` 单调递增；允许空洞；失败候选已分配的 Revision 不复用。
`Revision` 不是 Asset GUID，也不是 Manifest 的格式版本。

**K1.4** 发布前必须完成全量校验：格式版本、字段类型、规范 GUID、重复 GUID/路径、
引用文件存在性与校验和、Scene 解析。任一失败 → 不交换 `current`，旧 `current` 仍权威。

**K1.5** 候选文件先落盘（flush/fsync 或平台等价）；`current` 使用**单次原子替换**并落盘
（Windows `ReplaceFile` / `MoveFileEx(..., MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)`；
POSIX `rename` + 目录 fsync；具体形式在 M1-D 定）。两个文件各自 rename 不构成提交。

**K1.6** 提交是否发生只由 `current` 判定。进程在替换后、返回前崩溃，重启看到新
`current` 即为已提交；不得用内存中的成功/失败标志推断。

**K1.7** 交换成功后：新 revision 是唯一权威；此时才允许清 Dirty、发 Saved。交换后的
清理/元数据失败 → 操作结果 `CommittedCleanupPending`；**不回滚已提交 Revision**。

**K1.8** `current` 缺失/损坏：只允许显式恢复到有提交凭据的历史 revision；候选隔离；
无法判定时进入 `RecoveryRequired`，禁止保存与 Play。

**物理形态仍留 M1-D 设计**：单文件指针、指针目录、或带校验的指针记录都可以，但必须
满足 K1.2/K1.5/K1.6 的单一原子提交语义；「目录序 + 取最大 revision」不满足 K1.2。

### 3.3 K2 — Rename 两阶段 + 回滚协议

**K2.1 前置校验**：旧源已注册且可读；目标父目录存在；目标不冲突（按 K4 的 `PathKey`）；
目标与旧源在 `PathKey` 下不同（大小写-only rename 除外；相同则视为 no-op，不提交）；
目标规范路径在 `ProjectRoot` 内；GUID 已知。

**K2.2 准备**：把源字节复制到目标目录内的临时文件（**不移动唯一副本**），校验大小/哈希，
落盘后以不覆盖方式改名为目标；此时 Manifest 尚未改变。若目标在准备期间出现，回到
冲突处理。

**K2.3 提交**：使用 K1 的**同一提交机制**，把 Manifest 中该 GUID 的路径替换为目标规范
相对路径；GUID 不变。Scene 不需要改，因为 Scene 以 GUID 引用资产。提交点是 `current`
交换。

**K2.4 清理**：提交成功后删除旧源。删除失败 → 旧源保留为可恢复副本；操作结果
`CommittedCleanupPending`；清理可重试且幂等；**不回滚**。大小写-only rename 的清理
只删除中间临时名，不得删除最终目标。

**K2.5 提交前失败**：删除/隔离新副本；旧源与旧 `current` 不变；结果 `NotCommitted`。
`current` 交换失败同样处理；不得出现半更新注册表。若交换结果不确定（如进程崩溃），
按 K1.6 以 `current` 判定，不得凭内存标志宣布 `NotCommitted`。

**K2.6 不在提交前物理移动/删除旧源。** 旧 M1-0 §5 的「物理改名成功、注册表失败再回滚」
由本协议取代。copy-commit-delete 使「回滚失败导致旧身份丢失」不再是 rename 的正常
失败路径。

**K2.7 `RecoveryRequired` 的触发条件**：提交状态不可判定；或旧身份/源字节完整性已被
破坏；或 `current` 与注册表不一致且无法证明哪个是权威。进入后拒绝保存与 Play，等待
显式恢复；不得伪报旧状态可启动。这是对 C05「恢复失败进入 RecoveryRequired」的精确化：
copy-commit-delete 下，提交前失败不会破坏旧身份，因此 RecoveryRequired 只保留给
身份或提交状态不可证明的情形。

**K2.8 目标已存在**：显式冲突；不静默覆盖；即使内容哈希相同也不自动合并。
若确认覆盖的是同一注册资产的 reimport → 保持 GUID（见 K3.6）；若意图是新资产 →
换新路径并分配新 GUID（见 K3.2）。

**K2.9 路径消费者**：任何以路径而非 GUID 保存引用的消费者，必须在同一提交中更新，
否则拒绝 rename；禁止半更新。

**K2.10 源文件可靠写入**：`ScriptSave` 使用同一 temp + 校验 + 替换原语，失败保留原
文件；覆盖复制归 M1-E，同样不得静默覆盖。`C05` 的源字节保护要求不因 rename 协议
改写而消失。

### 3.4 K3 — GUID 身份契约 v1.1（S09 增补）

**契约版本**：Content Identity Contract v1.1。磁盘格式以附加、可选字段实现，旧场景
仍可读；若实现证明必须升 schema 版本，单独裁决，不在 M1 中静默升版。

**K3.1** Asset GUID = 身份；路径是 Source Identity 的一部分。GUID 不由路径或内容派生；
rename/move 不变。

**K3.2** 新导入分配**新** GUID；内容哈希相同不构成复用或合并身份的理由；重复内容
产生不同 GUID。内容寻址去重不是 M1 能力。

**K3.3** 删除不回收 GUID；Manifest 移除不把 GUID 释放给未来导入。

**K3.4 缺失资产**：Scene binding 保留原 GUID；实体降级保留；保存往返保留原 GUID；
禁止静默清空。这是 S09 从「警告降级」升级为 v1.1 契约的核心。

**K3.5 重新挂接**：只有显式的「这是同一资产」reattach 才恢复原 GUID；仅内容匹配
不自动挂接。外部移动按 delete + import 处理，不按哈希复用身份。

**K3.6 重新导入同一注册资产**（同一路径或显式选择该资产）：保持 GUID，更新 Source
Identity（路径/字节）。这是更新，不是新导入；不要因为内容变了就换 GUID。

**K3.7** Manifest 严格加载时统一 canonical GUID（`ToHex`）；输入的大小写/分隔符先
规范化，再检测重复；存储与比较都使用规范形式（S02）。

**K3.8** 所有消费者以 GUID 为身份，路径只用于定位；不得以路径相等替代 GUID 相等。

### 3.5 K4 — ProjectRoot 与 Windows 路径规范化

**K4.1 ProjectRoot**：打开时显式提供；解析为绝对、规范路径；会话期间固定。不得从
CWD 推断，也不得从 Manifest 的父目录猜测。旧工程由兼容入口显式提供 legacy root。

**K4.2 Manifest 规范形式**：UTF-8；项目相对路径；统一 `/` 分隔；无盘符、无 UNC、
无前导 `/`；无 `.`/`..`；无尾分隔；保留作者大小写。

**K4.3 解析**：`ProjectRoot` + 相对路径 → 规范化 → 包含检查。越界即拒绝；不得回退
CWD；不得静默改指向另一个文件。

**K4.4 旧格式兼容**：旧 Manifest 的绝对路径/旧相对路径必须由兼容入口提供 legacy root；
找不到则结构化失败；下一次显式保存迁移为规范相对路径；不静默重写，不猜根。

**K4.5 大小写规则（显式）**：存储保留大小写；比较使用显式 `PathKey`：
大小写不敏感卷用 ordinal-ignore-case，大小写敏感卷用 ordinal。卷能力必须显式判定，
不得按操作系统猜测；Windows 的 per-directory case sensitivity（如 WSL 目录）属显式
例外。两个条目在 `PathKey` 下相同 → Manifest 加载失败，禁止 last-wins。

**K4.6 大小写-only rename**：两阶段临时名 + 冲突检查；GUID 不变；在大小写不敏感卷上
不得与另一条目碰撞。

**K4.7 符号链接/junction（显式例外）**：默认拒绝经过链接的路径；先做词法规范化与包含
检查。若显式允许 root 内链接，则解析 real path 后重新检查包含关系；real path 越界 →
拒绝；链接环 → 拒绝；并记录 real path 诊断。

**K4.8 UNC/长路径（显式例外）**：UNC 只允许作为显式 `ProjectRoot`；Manifest 相对路径
不得含 UNC；`\\?\` 长路径前缀的支持或拒绝必须在 M1-A 明确，不得静默截断或静默失败。

**K4.9 验证**：任意 CWD 打开、工程搬迁、大小写冲突、越界、链接越界、旧格式兼容，
全部进入 M1-A 门禁。

### 3.6 M1-A ~ F 新顺序与门禁

| 步骤 | 内容 | 依赖 | 出口/故障注入 |
|---|---|---|---|
| **M1-A** | ProjectRoot + 规范路径：绝对根、相对路径规范形式、legacy root 兼容读、大小写/链接/越界规则 | K4 | 任意 CWD、搬迁、大小写冲突、`..`/链接越界、旧格式兼容 |
| **M1-B** | Manifest 严格事务加载：候选全量校验后整体交换；规范 GUID、重复 GUID/路径、未知类型拒绝 | M1-A、K1.4 | 损坏 JSON / 半条目 / 大小写 GUID 重复 / 未知类型 → 失败且内存旧表不变 |
| **M1-C** | Rename 两阶段：prepare → commit → cleanup；旧源保护；K3 身份保持；`ScriptSave` 可靠写入 | M1-B、K1、K2、K3 | 目标被占用 / 提交前失败 / 指针交换失败 / 清理失败 / GUID 保持 / 无半更新 |
| **M1-D** | 单一提交点 + Revision：Manifest+Scene 完整 revision、全量校验、原子 `current`、恢复边界 | M1-C 的同一提交原语、K1 | Manifest 或 Scene 写失败 / 指针替换失败 / 重启只得旧完整或新完整 / 候选不升级 |
| **M1-E** | 导入服务汇点：拷贝、冲突、目标目录、GUID 分配/保持、S09 保留与 reattach；UI 与无头同路 | M1-A~D、K3 | 覆盖冲突显式拒绝/确认；UI 与无头一致；新导入不复用 GUID；缺失 GUID 往返保留 |
| **M1-F** | 全链故障注入门禁：V1–V6 统一测试与证据入基线 | M1-A~E | 每个中断点重启后只能得到旧完整或新完整工程 |

**证据纪律**：M1-F 只记录实际运行的用例数与结果；不得预填通过数；构建警告按真实数量
记录；既有未判定失败保持未判定，不因 M1 复跑通过而改写为已知根因。

**M1-C 与 M1-D 的共享原语**：M1-C 的 rename 提交必须调用 K1 的同一提交接口，**不得**
另写「直接覆盖 Manifest」的第二条提交路径。该接口的最小实现可在 M1-B/M1-C 落地；
M1-D 负责 Revision、双文件发布、恢复与完整门禁。若实现中发现循环依赖，允许在 M1-C
内抽出内部 seam，但 M1-D 必须收口为唯一生产路径。

**M1-E 的 S09 归属**：Scene binding 的「保留原 GUID」是 M1-E 的实现项（与旧 M1-0 §8 Q3
的分配一致），但其格式兼容性必须在 M1-B 的加载路径中成立；M1-F 验证往返。M1-E 不得
用「内容哈希相同」自动把缺失引用挂到新导入的资产上。

**M1 不启动 M2**：真实导入器、Library、Build Key、Artifact Hash、Cooking、流送均不在
M1；M1 只建立可靠性基础。

### 3.7 六项必须验证

| 编号 | 要求 | 主要阶段 | 失败判据 |
|---|---|---|---|
| **V1** | 提交前失败绝不被看成已提交 | M1-C / M1-D | 清了 Dirty、发了 Saved、旧 `current` 不可打开、出现半注册表 → 失败 |
| **V2** | 提交后失败必须区分「已提交 / 清理未完成」 | M1-C / M1-D | 把清理失败报成提交失败、回滚已提交 Revision、重启得到旧版 → 失败 |
| **V3** | 重启只加载完整的已提交版本 | M1-B / M1-D / M1-F | 未提交候选被升级、半 revision 被接受、`current` 损坏时静默猜测 → 失败 |
| **V4** | rename 保持 GUID 身份 | M1-C | Manifest 条目 GUID 改变、Scene 引用改变、清理失败导致身份丢失 → 失败 |
| **V5** | 打开工程与 CWD 无关 | M1-A | 任意 CWD 下解析结果不同、回退 CWD、搬迁后 Import/Rename/Save/Open 不一致 → 失败 |
| **V6** | 现有格式/路径规则不变（兼容性读法） | M1-A / M1-B / M1-F | 旧 Manifest/Scene 不可读、GUID 身份规则被改、schema 静默升版、旧路径无兼容入口 → 失败 |

**V5 与 V6 的歧义消解**：V6 的「现有格式/路径规则不变」读作——既有格式版本、GUID
身份规则，以及除 C01/K4 明确修正的 CWD 锚定缺陷之外的路径语义不变。它**不**表示保留
CWD 相对解析；S05 记录的是缺陷，必须被 K4 取代。V5 要求消除 CWD 依赖，V6 要求其余
兼容性不被破坏，两者不冲突。

### 3.8 提交状态词汇（唯一四态）

所有保存/rename/import 结果只能落在以下四态；不允许「未知但看起来成功」。

| 状态 | 含义 | `current` | 允许动作 |
|---|---|---|---|
| `NotCommitted` | 提交前失败；旧版本仍权威 | 旧 | 修复后重试；旧工程可完整打开 |
| `Committed` | 提交完成且清理完成 | 新 | 继续；可打开/Play |
| `CommittedCleanupPending` | 提交完成，清理未完成 | 新 | 重试清理；**不回滚**；可打开/Play |
| `RecoveryRequired` | 提交状态或身份完整性不可判定 | 未知/损坏 | 阻止保存与 Play；显式恢复；可只读诊断 |

判定依据是 `current` 与持久记录，不是内存返回值。若无法证明处于前三态，一律
`RecoveryRequired`。

每个失败路径必须同时给出：提交状态、恢复动作、可执行证据用例。**「全部回滚」不是
可接受的默认答案**；已提交的 Revision 不得因后续清理失败而被回滚。

### 3.9 M1-0 文档的版本化更正

在 `Content-v2-M1-0-Source-Audit-Contract.md` 头部加 errata 注记，正文不删改：

- 保留 S01–S09 / C01–C09 的审计原文；
- §6 的旧 M1-A~F 仅作历史引用，新工单使用 §3.6；
- §8 的四个开放问题按 §3.10 读取；
- §9 的「之后进入 M1-A Manifest...」是旧下一步，新的 M1-A 是 ProjectRoot。

### 3.10 M1-0 §8 四项开放问题的裁决

| 原问题 | 裁决 |
|---|---|
| 1. commit 指针物理形态（单文件 vs 目录序） | 语义冻结为 K1 的**单一原子提交指针**；物理形态（文件/目录/记录格式与平台替换原语）在 M1-D 定。目录序 + 取最大 revision 不满足 K1.2 |
| 2. rename 旧源副本保留策略（隐藏目录 vs 事务日志） | 语义冻结为 K2 的 copy-commit-delete：提交前旧源不动；提交后清理失败保留旧源为可恢复副本。物理保留方式在 M1-C 定，必须可恢复、可重试、幂等 |
| 3. S09 保留原 GUID 是否触发 v1 契约升级 | 是。升级为 K3 的 Content Identity Contract v1.1；M1-E 实现，磁盘格式附加/可选，旧场景可读；若必须升 schema，单独裁决 |
| 4. Windows 大小写不敏感路径策略 | 由 K4 冻结：保留大小写、显式 `PathKey`、卷能力显式判定、大小写-only rename 两阶段、冲突失败不 last-wins；per-directory case sensitivity 与链接为显式例外 |

---

## 4 · D-3 — 编辑器生产主线

### 4.1 事实

| 编辑器 | 当前事实 | 定位 |
|---|---|---|
| `editor_avalonia` | 有 `Native/EditorBridgeApi.cs`、`Services/EditorHostService.cs`、`Services/ProductionPathGate.cs` 等真实 bridge/导入/生产路径门禁；v0.3 资格为 `PASS — scope-bound` | **生产编辑器主线** |
| `src/Nova.*` | `SampleSceneFactory` / `SampleProjectFactory` / `PlaceholderViewportHost`；UI 基础与 Dock 原型；未接 bridge/引擎生产路径 | **原型**，不是生产编辑器 |

### 4.2 裁决

1. **`editor_avalonia` 是唯一生产编辑器主线。** 所有生产编辑器功能、缺陷修复、
   验收证据以它为准。
2. **Nova 保持原型。** 其 UI foundation / Dock 工作是原型资产，不得作为生产验收
   证据，也不得被写成「另一套生产编辑器」。
3. **不并行维护两套编辑器。** 不要求同一功能在两边同步；不以「Nova 已有」作为
   `editor_avalonia` 的需求或验收依据。
4. **未来迁移必须单独提案。** 若要把 Nova 提升为生产编辑器，提案至少包含：
   收益证明、范围与兼容性、迁移/回退路径、退出条件、以及对 `editor_avalonia`
   的冻结安排。没有提案与裁决，不得以渐进方式事实上迁移。
5. **本裁决不删除 Nova、不改 CI、不改其 ADR。** Nova 的原型构建/测试可继续作为
   原型健康检查，但不进入生产门禁；其失败不得被用来证明或否决 `editor_avalonia`
   的生产能力。D10 多显示器/Dock 工作仍按既有记录保持暂停、非阻塞。

### 4.3 T-3 的近期方向

T-3 不是与 T-2/T-1 并行的第三条实现线，而是它们的**编辑器验收面**：

- 在 `editor_avalonia` 上闭合：真实工程打开、组件编辑、保存/重载、运行时行为；
- T-2 提供可靠导入/保存/重载/事务语义，T-1 提供 RigidBody 的 Inspector 与运行时闭环；
- T-3 的证据必须来自真实控件与真实 bridge/engine 路径，不能来自测试专用伪入口，
  也不能来自 Nova 原型；
- 任何 T-3 需要的 bridge ABI 变更沿用既有版本化纪律，不为验收临时开旁路。

### 4.4 不改变的东西

- `editor_avalonia` 的 v0.3 `PASS — scope-bound` 不被扩大解释为通用生产批准；
- Nova 的 ADR / Dock / D10 记录保持原状，不因本裁决改判；
- 本裁决不改变既有 bridge ABI、渲染生产路径（OpenGL 生产栈；RHI 仍为替代/sandbox 面）；
  未来 T-3 如需 ABI 变更，按 §4.3 的版本化纪律单独处理。

---

## 5 · 后续执行顺序（本轮均不启动）

| 阶段 | 内容 | 入口条件 |
|---|---|---|
| Stage 1 | **T-2 Content v2 M1-A~F**，按 §3.6 顺序，过 V1–V6 | 本记录冻结；M1-0 errata 生效 |
| Stage 2 | **T-1 RigidBody 闭环**：T1-A 契约 → T1-B 序列化/冷启 → T1-C 真实物理绑定 → T1-D Lua v2.1 → T1-E Inspector → T1-F 8 步全环门禁 | T-2 完成；F2-C 契约冻结 |
| Stage 3 | **T-4 GPUProfiler** 真实时间戳（Vulkan/GL + Tracy） | T-1 完成 |
| Stage 4 | **T-6 Console autoexec/CVar 线程安全 → T-7 Audio EFX/线程安全 → T-5 DebugDraw** | T-4 完成 |
| Stage 5 | T-8 资产流送/Cook、T-9 NavMesh、T-10 Networking，仅按真实产品需要启动 | 前序完成 + 真实需求 |

- **#33**（Host 写入失败对账/事务策略）与 **#34**（v0.3 Sprint 0 架构冻结）继续独立
  跟踪；其他验收通过不自动关闭它们。
- **D10-D 多显示器验收**保持暂停、非阻塞；不因本记录恢复，也不改变 D10 的既有命名
  与状态。
- 本记录**不授权** M1-A、T-1 或任何 P1 开工；下一阶段需单独指令。

---

## 6 · 刻意保留的开放项

1. `F2-S1` 的阶段归属（F2 / F3 之间）——待 F2→F3 映射冻结。
2. F2-D 的最终内容（Prefab / Physics Material 或其他）——未分配，不得自行占用。
3. M1-D 的 `current` 物理格式与平台替换原语——K1 语义已冻结，形式待 M1-D 设计。
4. M1-C 的旧源副本保留载体（隐藏目录 / 事务日志 / 其他）——K2 语义已冻结，
   形式待 M1-C 设计。
5. M1-E 的 S09 字段/格式与 schema 版本——K3.4 已冻结「保留 + 往返」，
   附加字段形式待 M1-E 设计；若需升 schema，单独裁决。
6. K4 的链接/UNC/长路径精确策略——默认拒绝越界已冻结，具体支持矩阵待 M1-A 明确。
7. M1-C / M1-D 共享提交原语的实现切分——不得出现第二条提交路径，切分方式待设计期定。

---

## 7 · 完成条件与非授权声明

本记录完成 Stage 0 的条件：

- D-1、D-2、D-3 三项裁决均已落到本记录；
- `Engine-Foundation-F2-Charter.md` 与 `Content-v2-M1-0-Source-Audit-Contract.md`
  各加 errata 注记，原文保留；
- 未写生产代码、未运行构建或测试、未执行 Git 写操作；
- 未改动 `third_party/` 子模块、`crashes/`、既有未判定失败记录与证据数字。

本记录**不**表示 M1-0 的实施已通过，也**不**表示 F2-C 或 T-1 已开工。下一步若进入
T-2，必须单独下达阶段指令，并从 M1-A（ProjectRoot + 规范路径）开始。
