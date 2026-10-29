# Content v2 M1-B：启动检查发现的阶段顺序冲突（待裁决）

**日期**：2026-10-28。**状态**：已裁决（选项 A，冻结路线）；M1-B 按该范围实现完成，
见 `docs/Content-v2-M1-B-Strict-Load.md`。
**未执行**：任何 Git 写操作。

---

## 1. 本次授权对 M1-B 的要求（摘要）

- 第一步：从**持久化的 `current`** 读取已提交版本及其 `Revision`；校验指针结构与引用关系；
  只加载该指针引用的 Manifest；禁止扫描最大 Revision；禁止自动晋升孤立候选。
- 第三步：`current` 缺失/损坏/格式非法/引用 Manifest 不存在、Revision 与内容不一致、
  截断/缺字段/版本不支持、重复 GUID、重复 `PathKey`（含大小写差异）、引用不一致/路径非法、
  存在更高 Revision 的孤立候选而 `current` 仍指向旧版本——均在拒绝/不得加载之列。
- 无法证明提交状态 → `RecoveryRequired`。
- 同时：本阶段**不**包含 Rename、统一写提交或导入服务重构。

## 2. 冻结契约与仓库现状（证据）

1. `docs/Roadmap-Decision-Record-2026-10-28.md` §3.2 **K1.2**：`current` 是唯一提交点；
   读取端只认 `current`；禁止扫描目录取最大 revision；禁止把未提交候选升级为 `current`。
2. 同文件 K1 末尾（约第 195–196 行）：**“物理形态仍留 M1-D 设计：单文件指针、指针目录、
   或带校验的指针记录都可以”**。即 `current` 的磁盘形态与替换原语明确属于 M1-D。
3. 同文件 §3.6 阶段表（约第 305–307 行）：
   - **M1-B**：只要求“Manifest 严格事务加载：候选全量校验后整体交换；规范 GUID、
     重复 GUID/路径、未知类型拒绝”，**未包含** `current`/Revision；
   - **M1-D**：才包含“Manifest+Scene 完整 revision、全量校验、原子 `current`、恢复边界”；
   - 依赖列：M1-B ← M1-A；M1-C ← M1-B；M1-D ← M1-C。
4. 同文件第 372、444 行再次确认：commit 指针物理形态在 M1-D 定；K1 语义已冻结、形式待 M1-D。
5. 代码与磁盘现状（本次检查）：
   - `engine/include/Engine/Core/Content`、`engine/src/Core/Content`、`bridge/src` 中
     没有 `current` 指针、`Revision`、候选目录或 `RecoveryRequired` 的任何实现；
   - `assets/` 下没有 `current`、`revisions/` 或等价指针文件；
   - 现有工程（`assets/gp01/manifest.json`、`assets/scenes/*.manifest.json`）只有
     manifest + scene，`ContentRegistry::LoadManifest` 直接接收 manifest 路径；
   - 因此“持久化的 `current`”在当前仓库中尚不存在。

## 3. 冲突

M1-B 的 `current` 读取要求与以下两点构成实质冲突：

1. **契约归属**：K1 明确把 `current` 的物理形态留给 M1-D；M1-B 若定义或实现该格式，
   等于提前改写 M1-D 的交付边界。M1-B 的退出门禁要求“`current` 是唯一已提交版本依据”，
   而当前没有任何生产者在写 `current`。
2. **顺序与兼容**：M1-D 依赖 M1-C，M1-C 依赖 M1-B。若 M1-B 需要读取 M1-D 才定义的指针，
   阶段顺序倒置。并且按“`current` 缺失 → 严格拒绝”落地，现有全部工程（包括 M1-A
   门禁使用的 GP01）将无法打开；M1-B 又明确不授权“统一写提交”，无法在打开时生成
   `current` 或迁移旧工程。这会使 M1-A 已验收的打开行为立即回退。

## 4. 候选处置（需裁决）

| 选项 | 内容 | 影响 |
|---|---|---|
| **A. 按冻结路线（推荐）** | M1-B 只做 §3.6 行的严格候选加载：完整解析 → 全量校验（格式版本/字段类型/规范 GUID/重复 GUID/`PathKey`/引用一致性/Scene 解析）→ 整体交换；失败保留旧内存状态。`current`/Revision/`RecoveryRequired` 的读取与判定归 M1-D | 不改冻结契约；现有工程不受影响；用户 M1-B.1 的 `current` 读取推迟到 M1-D |
| **B. `current` 只读前移 + LegacyUnversioned 兼容** | M1-B 定义 `current` **只读**格式并严格校验收损指针；无 `current` 的旧工程进入显式兼容加载（现状 manifest+scene；不自动生成指针） | 需要裁决把 K1 的“物理形态在 M1-D”修订为“只读格式在 M1-B、原子写与替换原语在 M1-D”；同时新增 `LegacyUnversioned` 状态，属于 K1.8 之外的新状态 |
| **C. `current` 只读前移 + 严格拒绝缺失** | 完全按 M1-B.1 文本；`current` 缺失即失败 | 现有工程在 M1-D 之前不可打开；M1-A 验收行为回退；需要额外的迁移准备，不建议 |

## 5. 裁决结果（2026-10-28）

裁决选择**选项 A（按冻结路线）**：M1-B 只实现严格候选加载与内存整体交换；
`current`、Revision、`RecoveryRequired` 与孤立候选判定归 M1-D。

据此，本文件 §1 中“从持久化 `current` 读取”的要求不在 M1-B 实施；§2 的冲突证据
保留原样。M1-B 的实现、门禁与实测结果见 `docs/Content-v2-M1-B-Strict-Load.md`。

## 6. 原等待事项（已关闭）

裁决前未写任何 M1-B 代码；M1-A 的收尾工作（裁决写入交付记录、`OnImportAsset`
的 CWD 边界闭合）已独立完成。
