# 资产管线现状分析与下一版本实施方案

日期：2026-09-16

## 一、总体结论

项目已具备小规模内容生产闭环（GUID 身份 → 导入/绑定 → 保存 → 冷启恢复），尚未形成真实导入 → 派生产物 → 增量构建 → 发布的完整管线。

下一版本定位：Content Pipeline v2 = 可靠资产操作 + 工程可迁移 + 真实导入 + 本地增量。

优先级：数据一致性 → 导入真实性 → 可重建性 → 发布 → 性能。

明确不做：启用旧 AssetPipeline、完整 Cooking、分布式 DDC、全格式模型导入、流式加载。

## 证据边界

- 静态核查：engine/Core/Content、engine/Core/Resources、bridge、editor_avalonia 及历史验收文档
- 运行现有 test_content.exe：42/42 通过（约 7.1s，需项目惯例 ASAN_WIN_CONTINUE_ON_INTERCEPTION_FAILURE=1）
- 未重新构建二进制，结果不能证明对应当前全部工作区源码；test_bridge / Avalonia 未重跑
- 工作区存在未提交修改（Phase 7 viewport 相关）；本次未修改任何源码
- 主流对照基于 Unity / Unreal / Godot 官方文档

## 二、现状：生产主线与遗留框架

### 2.1 生产主线（Content v1，已验证）

- Avalonia Asset Browser (Import/Rename/Assign) → EditorSession C-ABI (bridge/src/EditorSession.cpp:521, :556)
- ContentRegistry（GUID↔路径 + Manifest，engine/src/Core/Content/ContentAsset.cpp:17）
- SceneSerializerV1 / ResourceLifecycle → 保存 / 运行 / 冷启动恢复

| 能力 | 判断 | 证据 |
|---|---|---|
| GUID 身份、导入幂等、双向冲突检测 | 已具备 | ContentAsset.cpp:17、:33 |
| Manifest 持久化（正常路径） | 已具备 | ContentAsset.cpp:87 |
| 改名保留 GUID、多场景共享、冷启恢复 | 已验证 | docs/Dogfood09-Ledger.md R1–R8 |
| Bridge 导入/改名/会话稳定索引 | 已接入 | EditorSession.cpp:521、:542、:556 |
| 生产资产类型 | 仅 Texture / Script | ContentAsset.h:33 |
| 格式转换 / 派生产物 / Cooking | 无闭环 | ContentAsset.h:11（v1 红线） |

关键定性：当前 Import 约等于登记文件身份，只检查文件存在（EditorSession.cpp:530），不验证内容可解码、类型与声明一致，无派生产物。

### 2.2 旧 AssetPipeline 族是遗留框架，不能计为完成能力

项目自身审计已标其为零生产消费者（ResourceLifecycle.h:8-15）。直接启用存在实质风险：

| 问题 | 位置 |
|---|---|
| 纹理预处理只登记输出路径不写产物；Shader 编译模拟成功 | AssetPipeline.cpp:112、:192 |
| 完整性检查/修复返回默认成功 | AssetDatabase.h:121-124 |
| 依赖图节点指向局部条目副本（悬空指针） | AssetPipeline.cpp:419-431 |
| 拓扑排序结果被反转、并发不等待依赖 | AssetPipeline.cpp:625-627、:669 |
| Shader 输出按 stem 命名，foo.vert/foo.frag 冲突 | AssetPipeline.cpp:204 |
| .meta 读取把任何 type 当 Texture、不恢复导入设置 | AssetMetaDb.cpp:171-182 |
| Editor 路径解析依赖进程 CWD，注释与实现不符 | EditorSession.cpp:808 vs EditorSession.h:195 |

处置：冻结旧管线，禁止新增生产消费者；可借鉴算法思想，不整体复用实现。

## 三、演进脉络与主流对照

### 3.1 本仓库演进（方向正确，应延续）

早期通用框架（多套数据库/Pak/VFS 接口）→ Content v1 收窄为 Texture/Script + GUID 契约 → Dogfood/VS01 验证生产闭环 → GP01 规模验证（33 资产）→ Avalonia Phase 3-C/G3 冻结（Import/Rename ABI + 生产就绪）。方向从堆框架转向真实生产驱动。

### 3.2 主流参考（Unity / Unreal / Godot 官方资料）

| 维度 | Unity | Unreal | Godot | 本项目 |
|---|---|---|---|---|
| 资产身份 | GUID + .meta，场景不存路径 | Package/Object 引用 | ResourceUID | 已对齐 |
| 源/产物分离 | SourceAssetDB vs ArtifactDB，产物可重建 | 派生数据进 DDC | .import 配置 + 隐藏产物目录 | 缺失，源即资产 |
| 导入配置 | 配置是导入输入，改动触发 reimport | Interchange Pipeline 可配置 | 每资产配置 + 预设 + MD5 自动重导入 | 遗留且不工作 |
| 依赖与增量 | DependsOnArtifact 显式声明、闭包 reimport | DDC + 增量 Cook | 源 MD5 触发重导入 | 无增量 |
| 共享缓存 | Accelerator | Zen Storage Server | 无 | 无（本项目规模无必要） |
| 发布链路 | 构建管线 | Cook（IoStore/Zen），与 DDC 分离 | 导出打包 | Pak 有骨架未接入 |

### 3.3 值得借鉴的四个原则（按本项目规模裁剪）

1. 三类标识分离：Asset GUID（场景引用，改名不变）/ Build Key（源内容+设置+依赖哈希，决定是否重导）/ Artifact Hash（输出校验）。当前只有第一类。
2. 导入产物是可再生派生数据：产物丢失不等于资产丢失；Library 类目录应可随时删除重建。
3. 依赖显式声明 + 闭包失效：源变更只重导受影响闭包；漏报依赖比多报更危险。
4. 内容寻址缓存：产物按内容哈希存储，天然去重、可校验；分布式共享对本项目规模无必要，后置。

## 四、问题与风险排序

### P0（数据一致性，必修）

| # | 问题 | 证据 | 影响 |
|---|---|---|---|
| 1 | 改名回滚不完整：注册表更新失败只回滚物理文件，Unregister 掉的旧条目未恢复，回滚 rename 错误码未检查 | EditorSession.cpp:587-593 | 文件在、身份丢，该资产所有 GUID 引用失效 |
| 2 | Manifest 加载无事务性：Clear() 后逐条填充、绕过 RegisterExplicit 冲突检查、无版本校验、字段异常未整体捕获 | ContentAsset.cpp:104-136 | 半损坏 manifest 产生部分注册表，甚至注入 GUID/路径冲突 |
| 3 | SaveProject 非原子：scene 写成功、manifest 写失败即返回，磁盘留下互相不一致的快照 | EditorSession.cpp:132-147 | 崩溃/断电后冷启动得到混合状态 |

### P1（可用性，随后补齐）

| # | 问题 | 证据 |
|---|---|---|
| 4 | 路径解析锚定进程 CWD（头文件注释写锚定 manifest 目录，与实现不符；改名重算路径也依赖 CWD） | EditorSession.cpp:808-816、:588 vs EditorSession.h:195 |
| 5 | 导入不验证内容与类型一致（.lua 可登记为 Texture） | EditorSession.cpp:530-536 |
| 6 | 文件拷贝策略在 UI 层硬编码 assets/gp01/、同名静默覆盖 | MainWindow.axaml.cs:202-239 |
| 7 | 无资产删除/移动契约；manifest 快照式保存需显式触发（Dogfood09 已记录该坑） | ContentAsset.cpp:79、docs/Dogfood09-Ledger.md:44 |

### P2（规模与体验，可后置）

- AssetAt 每次拷贝全表线性扫（EditorSession.cpp:782），逐项枚举 O(n^2)。
- 会话索引 m_AssetOrder 跨会话不保证稳定（重开按 manifest 序）。
- 无文件监视驱动的自动 reimport（FileWatcher 未接 ContentRegistry）。

## 五、实施前必须修正的设计

### 5.1 保存：两个文件分别替换不等于整体原子

SaveProject 分别保存场景与 Manifest（EditorSession.cpp:132），存在混合快照风险。但先写两个临时文件再分别 rename，仍可能在两次替换之间崩溃，不能承诺整体原子性。

建议采用版本化快照 + 单一提交指针：写入 revision N 的两文件 → 校验完整性 → 原子替换 current 指针 → 读取端只加载 current 指向的完整版本。发布失败继续用旧快照；启动时识别未提交版本不当作有效工程；断电持久性需 fsync 及平台语义验证。

若本期不便改变工程布局，可改用事务日志 + 启动恢复，但必须明确恢复协议。

### 5.2 配置：导入设置属于权威数据，不能只存 Library

GUID、源路径、导入设置应进入版本控制；Library 只存可重建数据（派生产物、指纹、依赖索引、缩略图）。下一版直接扩展现有 Manifest 即可，不必同时引入 .meta 和新数据库。删除 Library 后必须能按权威数据重建全部产物，GUID 与设置不变。

### 5.3 路径：先引入工程根目录，不能直接改成 Manifest 相对路径

现有 Manifest 使用 assets/gp01/... 形式路径，直接相对 Manifest 目录解析会重复拼接目录并破坏旧工程。应引入明确 ProjectRoot，内部路径相对它保存；旧 Manifest 按旧规则读取，迁移后写入新版本；路径规范化处理分隔符与 . / ..；明确大小写与工程外资产策略。

### 5.4 验证：魔数正确不代表资产有效

纹理至少需真实解码，并限制尺寸、像素数与解码内存。Script 应使用现有 Lua 解析器做只编译不执行的语法检查；仅检查 UTF-8 不能证明脚本有效。区分三种状态：登记（身份存在）、导入（源验证通过+产物生成）、运行（产物加载成功）。已有效资产重导失败时保留 GUID 与上一有效产物并显示错误，不删除身份。

### 5.5 依赖：不是所有引用都触发重新构建

| 类型 | 示例 | 变化后行为 |
|---|---|---|
| 构建依赖 | Shader include、参与转换的外部文件 | 重新构建受影响产物 |
| 运行时引用 | 场景引用纹理、实体绑定脚本 | 更新加载或引用检查，不重写场景 |

只有构建输入变化才使产物失效；运行时引用变化由加载与引用完整性检查处理。

## 六、下一版本实施方案（两个里程碑）

### 里程碑 M1：可靠资产工作流（必达，约 5–8 天）

| 工作项 | 实施内容 | 验收标准 |
|---|---|---|
| Manifest 严格加载 | 校验版本、字段类型、空 GUID、重复 GUID、路径冲突；候选数据全部通过后一次性替换当前状态 | 加载失败不污染原注册表 |
| 改名一致性 | 操作前检查冲突；失败恢复身份映射；回滚失败单独报告，不宣称成功恢复 | 文件、GUID、路径、绑定及 Dirty 状态一致 |
| 工程保存恢复 | 完整快照提交或明确的事务日志恢复协议 | 任一写入或提交失败后，重启只能读到旧版或新版完整快照，不出现混合版本 |
| 工程路径独立 | 引入明确工程根目录；版本化迁移旧路径规则 | 工程搬迁、不同 CWD 启动后，导入/改名/保存/恢复结果一致 |
| 导入入口统一 | 文件复制、冲突处理、目标目录策略从 Avalonia 下沉到工程服务层 | UI 与无头入口行为一致；禁止静默覆盖不同资产 |
| 错误反馈 | 返回资产 GUID、操作、错误阶段、恢复结果 | 错误可定位；失败不发送成功事件 |

M1 不加入新资产类型、压缩算法或通用任务图。目标：现有导入、改名、保存流程可靠，工程不再依赖启动目录。

### 里程碑 M2：真实导入与本地增量（M1 通过后推进，约 8–12 天）

| 工作项 | 实施内容 | 验收标准 |
|---|---|---|
| 导入配置持久化 | 在版本化 Manifest 中保存设置与导入器版本；Library 只放缓存 | 删除 Library 后 GUID 与设置不丢失 |
| Texture 导入器 | 真实解码、尺寸与内存上限检查；生成运行时可读产物 | 损坏图片不能报告导入成功；有效产物实际加载通过 |
| Script 导入器 | 现有 Lua 解析器只编译不执行；首版可保留源文本产物 | 语法错误可定位；不以 UTF-8 正确代替语法验证 |
| 构建指纹 | 源内容、规范化设置、导入器版本、目标配置、构建依赖共同参与计算 | 任一实际输入变化正确失效；无变化不重导 |
| 增量导入 | 源变更只重导该资产及其构建依赖闭包；后续接 FileWatcher 自动触发 | 改 1 个文件只有受影响闭包重导，其余跳过并有跳过计数 |

### 总验收门禁

- 每里程碑 test_content / test_bridge 全绿 + 新增负路径/故障注入测试入 Evidence Baseline（当前 212）。
- M2 金路径：删 Library → 重开工程 → 自动重建 → 场景 GUID 契约字节级不变。
- 全程不改已冻结的场景 GUID 契约；Content v1 红线仅在 v2 正式立项时显式升级。

## 七、后续演进（明确不进本期）

- Pak/发布打包：等 M2 产物闭环稳定后接入现有 Pak 骨架（128 位哈希 + 构建期碰撞检测，见 docs/Pack-System-Summary.md 修正方案）。
- 模型/字体/Shader 资产类型：按真实生产需求触发再扩。
- 共享 DDC / 分布式缓存：团队规模或资产量级（千级以上）出现后再评估。
- 旧管线退役：DependencyGraphPanel 等旧面板迁移后，删除 AssetPipeline/AssetMetaDb/string-GUID AssetDatabase，消除双体系。

## 八、一句话总结

先让改名、加载、保存在任何故障下都不丢数据（M1），再让工程搬得动、导入验得真（M2 前半），最后把登记身份升级为可重建产物 + 本地增量（M2）。每步都有测试门禁兜底，旧框架冻结不背新债。
