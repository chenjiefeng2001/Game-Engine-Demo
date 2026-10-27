# Content Pipeline v2 — M1-0 Source Audit + Contract

日期：2026-09-16 · 状态：DRAFT（待评审裁决） · 前置：`docs/Asset-Pipeline-v2-Report.md`
纪律：本文件只做事实源审计与契约裁决，不含任何实现代码。M1-A 起才允许改代码。

## 0 · 证据边界

- 已逐行读取：`engine/src/Core/Content/ContentAsset.cpp`（138 行全文）、
  `engine/include/Engine/Core/Content/ContentAsset.h`、`bridge/src/EditorSession.cpp`
  （1064 行全文）、`bridge/src/EditorSession.h`、
  `engine/include/Engine/Core/Content/SceneSerializerV1.h` 与
  `engine/src/Core/Content/SceneSerializerV1.cpp`（217 行全文）、
  `engine/include/Engine/Core/Resources/ResourceGUID.h`（101 行全文）。
- 静态核对：`editor_avalonia/MainWindow.axaml.cs:202-239`（宿主导入拷贝）、
  `assets/gp01/manifest.json`（38 行格式实证）、
  `tests/test_content/ContentPipelineTest.cpp:106-110`（损坏 manifest 测试范围）、
  `docs/Engine-Foundation-F2B-Closure.md`（Evidence Baseline = 212、Gate 纪律）。
- 未执行任何构建或测试；`test_content` 42/42 是历史运行（Asset-Pipeline-v2-Report.md:18），
  不能证明当前工作区。
- 行号引用以 2026-09-16 工作区（含未提交 Phase 7 修改）为准，M1 实施时须复核。

## 1 · 审计裁决摘要

当前 Content v1 只有 GUID/path/type 注册表与分别写入的 Manifest、Scene；没有 ProjectRoot、revision、commit 指针，也没有 Imported/RuntimeReady 状态。M1 只建立可靠性基础，不启用真实导入器。

M1-0 的交付是契约草案与可执行验收清单，不是实现通过或正式冻结。评审确认后才进入 M1-A。

## 2 · Source Audit：现状与事务缺口

| 编号 | 源码事实 | 裁决 |
|---|---|---|
| S01 | ContentAsset.cpp:121 在逐条校验前 Clear；:123 跳过缺字段；:130 未知类型降级 Texture | Load 必须候选表完整验证后交换，失败保留旧表 |
| S02 | ContentAsset.cpp:132 用输入 hex 作键；ResourceGUID.cpp:59 接受大小写、分隔符 | 严格校验后统一 ToHex，按规范 GUID 检测重复 |
| S03 | EditorSession.cpp:136-140 先写 Scene 再写 Manifest | 两文件独立替换不是工程事务，须单提交点 |
| S04 | EditorSession.cpp:582-593 先改文件，后注销身份，回滚结果未检查 | 不可先破坏旧身份；回滚失败须 RecoveryRequired |
| S05 | EditorSession.cpp:808 相对 CWD；SceneSerializerV1.cpp:155 直接传注册路径给纹理加载 | ProjectRoot 必须贯穿所有消费者，不能只修一个 helper |
| S06 | EditorSession.cpp:623-639 原地截断写脚本；MainWindow.axaml.cs:230 覆盖复制 | 仅快照 Scene/Manifest 不能保护源文件字节 |
| S07 | SceneSerializerV1.cpp:53 清空 out，:77 字段转换可抛异常，:214 不包围结构转换 | Open 的候选快照与异常边界必须覆盖完整解析 |
| S08 | EditorSession.cpp:98-103 在候选实例化完成前 Stop/Reset GameplayAPI | 解析成功不等于 Open 事务成功，失败不得破坏旧会话 |
| S09 | SceneSerializerV1.cpp:145-168 缺资产时返回空 binding | 降级打开后直接保存可能丢失原 GUID；须保留原引用或阻止破坏性保存 |

以上为静态发现，不等于已完成故障复现。ContentAsset.cpp 位于 engine/src/Core/Content；ResourceGUID.cpp 位于 engine/src/Core/Resources；SceneSerializerV1.cpp 位于 engine/src/Core/Content；EditorSession.cpp 位于 bridge/src；MainWindow.axaml.cs 位于 editor_avalonia。

## 3 · 工程快照事实源

裁决 C01：工程入口明确指定 ProjectRoot，打开后固定为绝对规范路径；所有资产定位相对它，禁止操作期间重新读取 CWD。旧工程由兼容入口显式提供旧根目录，不能猜测 Manifest 的父目录就是根。

裁决 C02：revision 是工程提交序号，不是资产 ID 或格式版本。Manifest 与 Scene 属于同一个不可变 revision；current 是唯一提交点，不以目录中最大 revision 自动恢复。

裁决 C03：保存先准备完整候选 revision，校验全部文件及校验和，再发布 current。失败保留上一已提交版本；成功发布之后才能清 Dirty 和发送 Saved。两个文件分别 rename 不构成整体事务。

裁决 C04：启动只加载已提交且完整的 revision。未 commit 候选忽略并隔离；current 损坏时只允许显式恢复到有提交凭据的历史版本，禁止把未提交目录升级为有效工程。

裁决 C05：Scene/Manifest 快照不保护源文件字节。M1-B 的 rename 必须保留可恢复的旧源副本或持久事务记录；ScriptSave、覆盖复制同样必须纳入可靠写入边界。恢复失败进入 RecoveryRequired，禁止继续保存或 Play，不得伪报旧状态可启动。

## 4 · 资产身份与状态契约

裁决 C06：三个标识严格分离。Asset GUID 表示身份，改名移动不变；Source Identity 表示字节与规范路径，检测源文件被外部修改；Build Key 表示源加导入设置加工具版本，决定是否重导；Artifact Hash 表示具体产物字节。M1 只落 GUID 与 Source Identity，Build Key 与 Artifact Hash 属 M2，不得提前混入 GUID 字段。

裁决 C07：资产状态机为 Registered、Imported、RuntimeReady；失败保留上一有效状态并标记 StaleError，禁止用删除身份表示导入失败。Registered 在 M1 可视为暂定态，表示身份登记但未验证。

裁决 C08：打开工程时注册表中不存在但场景引用的 GUID，按现有 SceneSerializerV1.cpp:148-168 警告降级保留实体，但该 binding 必须保留原始 GUID，供修复和保存往返，禁止静默清除。

裁决 C09：会话稳定序 m_AssetOrder 保持只追加；打开工程按提交序重建，不按迭代序；迁移期仅允许显式登记序，禁止每次打开重排既有索引。

## 5 · 失败原子性裁决（M1 核心问题）

总原则：任一步失败，最后已提交的工程状态必须仍可完整打开；正在进行的操作要么完成，要么显式进入可恢复或已回滚状态，禁止留下半更新身份。

| 故障场景 | 裁决行为 |
|---|---|
| Manifest 写失败 | 保留上一已提交 revision；候选作废；返回结构化失败（阶段=manifest-commit） |
| Scene 写失败 | 同上，不提交任一文件；候选 revision 整体丢弃 |
| 提交指针更新失败 | 视为保存失败；旧 current 仍有效；候选进入隔离区等待清理或人工恢复 |
| rename 中途失败 | 目标路径占用等前置校验先行；物理改名成功但注册表失败时，尝试回滚并恢复旧身份 |
| rename 回滚也失败 | 进入 RecoveryRequired：GUID 旧身份丢失事实必须显式报告，禁止伪报已恢复；保存与 Play 拒绝直至人工恢复 |
| Manifest 损坏 | 严格解析失败 → 保留内存旧表（若有会话）或拒绝打开；禁止半填充注册表 |
| 新 snapshot 未 commit | 启动忽略候选；current 指向上一完整版本；不自动升级候选 |
| CWD 改变 | 解析结果与 CWD 无关（ProjectRoot 锚定）；打开时校验全部注册路径存在性 |
| Library 半生成 | M1 无 Library；此条为 M2 前置契约：产物缺失不等于资产丢失，可由权威数据重建 |
| ScriptSave 失败 | 现状原地截断（EditorSession.cpp:634）必须改为可靠写入：临时文件+校验+替换，失败保留原文件 |

关键不变式：任何操作序列中断后，磁盘上必须仍存在一个结构完整、可被 OpenProject 接受的最后已提交工程。

## 6 · M1-A ~ F 实施顺序与验收矩阵

| 步骤 | 内容 | 故障注入验收（全部须进 Evidence Baseline） |
|---|---|---|
| M1-A | Manifest 严格事务加载：候选全量校验（版本、字段类型、规范 GUID、重复 GUID/路径）后整体交换 | 损坏 JSON / 半条目 / 大小写 GUID 重复 / 未知类型 → 失败且内存旧表不变 |
| M1-B | Rename 事务：前置冲突校验 → 旧身份保护 → 物理改名 → 注册表更新 → 回滚协议 | 目标被占用 / 注册表更新失败 / 回滚失败 → RecoveryRequired 显式化 |
| M1-C | 工程快照提交：候选 revision → 校验 → 单提交点发布 | Scene 或 Manifest 写失败 / 指针替换失败 → 旧版本仍可完整打开 |
| M1-D | ProjectRoot + 路径迁移：旧 manifest 兼容读，新版本显式根 | 任意 CWD 启动、工程搬迁后 Import/Rename/Save/Open 结果一致 |
| M1-E | 导入服务统一：拷贝、冲突、目标目录从 Avalonia 下沉到服务层 | 覆盖冲突显式拒绝或确认；UI 与无头入口行为一致 |
| M1-F | Failure-Injection Gate：以上全部故障路径统一测试 | 每个场景中断后重启只能得到旧版或新版完整工程 |

门禁：每步全绿 + 新增测试计入 Evidence Baseline（当前 212，F2B-Closure §2）；M1 全部完成前不启动 M2 真实导入。

## 7 · 与 Engine Foundation 边界

Engine Foundation（F2-C RigidBody、F2-D Physics Binding）与 Content Foundation（M1/M2）并行但互不耦合；汇合点在"组件引用的资产必须来自稳定 Content Identity"，届时以本契约 C06 的 GUID 语义为准。M1 期间禁止在 F2 侧引入 Content 依赖，反之亦然。

## 8 · 遗留与开放问题（评审裁决项）

1. C02 的 commit 指针物理形态（单文件指针 vs 目录序）需在 M1-C 设计期定案；本文件只裁决语义。
2. C05 的 rename 旧源副本保留策略（隐藏目录 vs 事务日志）M1-B 设计期定案。
3. S09 的"保留原 GUID"需要 SceneSerializerV1 binding 模型小改（保留无法解析的 GUID 而非丢弃），M1-E 一并实施；涉及 SceneSerializerV1.h:67-70 结构扩展，需评审确认是否触发 v1 契约升级。
4. 大小写不敏感文件系统（Windows）上的路径规范化策略未裁决；M1-D 前须定案。

## 9 · 结论

M1-0 完成条件：本文档评审通过、开放问题 1-4 裁决、验收矩阵确认为 M1-A~F 的 Gate 表。之后进入 M1-A Manifest Strict Transactional Load。本阶段不改任何生产代码。
