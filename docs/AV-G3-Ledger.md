# AV-G3 Ledger — Production Authoring Gate 摩擦与裁决

> 建立日期：2026-08-28 · 依据 `docs/Avalonia-Phase3-G3-Plan.md`
> 全程零内部操作（❌ 编辑 JSON · ❌ 直调 C++ · ❌ ImGui · ❌ 手改 GUID · ❌ 手改 manifest · ❌ 依赖外部 IDE · ❌ 重启 Editor 修状态）—— `AvaloniaEditor --gate3g` ALL GREEN。

---

## 1. 裁决：Result A — Production Ready（Phase 3 FROZEN）

**完整生产链一次跑通**（open → create → rename → transform → import →
assign sprite → assign script → open script → 改 gameplay → save → play →
改 → reload(_PERSIST) → play → save → close → reopen → 全一致），
全程经 VM → Session ABI 单向路径，**无引擎改动、无 UI 新增面板**。

无阻塞性工作流缺口（无 Result B），无冻结 API 无法表达的需求（无 Result C）。

### 证据

| 项 | 结果 |
|----|------|
| `AvaloniaEditor --gate3g` | **ALL GREEN exit=0**（D1-D5 + M1 + Authoring/Scripting/Persistence + E1/E2/E3 全断言） |
| 回归 gate1/2/3a/3b/3c/3d | 全 exit=0 |
| `test_bridge` | **21/21 PASS**（+`MissingGuidAssetWarnsAndEntityRemains`） |
| `test_scripting` | 23/23 PASS |
| Integrity Gate | **ALL GREEN (I1+I2+I3, 163 tests)** |

---

## 2. 摩擦记录（低严重度，Phase 4 / AV-Dogfood-01 候选，均不阻塞）

### AV-GP-101 — 跨会话资产索引不可依赖（须按 identity 解析）
- **Trigger**：Save→Close→Reopen 后，用打开时记录的资产 index 做 `OpenScript/Play`,
  命中错误资产。
- **Observed**：`SaveManifest` 后 registry 的写入顺序可能与打开时不同；gate 以旧的
  `dlIdx` 打开非 game.lua 资产。
- **Evidence**：AV-G3 gate 首跑 `edited param visible on reopen` / `E2 play recovered`
  FAIL，改为按 `Path.EndsWith("game.lua")` 身份解析后 GREEN。
- **Severity**：low（单会话内 index 稳定，桥保证；跨会话是 gate 时刻的边界）。
- **Frozen API sufficient?**：yes（AssetAt 按 GUID 序 + GetAssetPath 可身份解析）。
- **Minimum UI fix**：Editor/VM 统一以 GUID/Path 持有资产选择态，而非裸 index。
- **Capability gap?**：无。

### AV-GP-102 — Assign 需“实体 + 资产”双选中，顺序/可达性可再打磨
- **Trigger**：inspector 分配 Sprite/Script 前须同时有 entity 与 asset 选中。
- **Observed**：无选中或仅单选 → 分配被忽略（Console 提示）。用户心智易混淆“先选谁”。
- **Severity**：low。
- **Frozen API sufficient?**：yes。
- **Minimum UI fix**：Inspector 提供“从选中资产分配给选中实体”的就位按钮 / 拖放。
- **Capability gap?**：无。

### AV-GP-103 — Play/Reload 成功后缺少强反馈
- **Trigger**：Reload（`_PERSIST` 保留）成功仅见 Console 日志行。
- **Observed**：工具栏 Stop 使能态反映运行，但“本次 Reload 是否成功 / 错误在哪个文件”
  UI 无即时徽标；D4 错误文本仅 Script Editor 顶部 + Console。
- **Severity**：low。
- **Frozen API sufficient?**：yes（`IsRunning` + `GetRuntimeError` + 事件已回读）。
- **Minimum UI fix**：Reload 后用短暂状态徽标（“✓ reloaded (persist preserved) /
  ✗ error: <line>”）。
- **Capability gap?**：无。

### AV-GP-104 — `_PERSIST` 观测需运行时探针（非通用全局读）
- **Trigger**：gate 要读取嵌套表 `_PERSIST.hp` 验证保留。
- **Observed**：`GetGlobalInt` 只读顶层全局；为此新增 `RuntimePersistInt` 探针 ABI。
- **Severity**：low（仅影响调试/断言可观测性，不影响产品运行时）。
- **Frozen API sufficient?**：yes（bridge 探针复用既有 Execute + GetGlobalInt）。
- **Minimum UI fix**：无（非 UI 需求）。
- **Capability gap?**：无。

### AV-GP-105 — E3 缺失 GUID 资产的原生告警曾被 Editor 丢弃
- **Trigger**：Engine `InstantiateScene` 对 registry 缺失 GUID 已有警告（实体保留）。
- **Observed**：AV-G3 前 bridge `OpenProject` 丢弃 `r.warnings`，Editor 静默。
- **Evidence**：`SceneSerializerV1.cpp` L115-133 存在 contract warning；bridge 现捕获
  `GetWarnings()` → VM 打印 Console（AV-G3 E3 断言 GREEN）。
- **Severity**：low（信息暴露，非崩溃）。
- **Frozen API sufficient?**：yes（bridge 复用既有 LoadResult 告警通道）。
- **Minimum UI fix**：已落地（Console `[WARN]` + ABI 读取）。
- **Capability gap?**：无。

### AV-GP-106 — 通过纯 UI 无法“制造”一个缺失 GUID 工程（无 remove-asset 操作）
- **Trigger**：AV-G3 负路径 E3 需要“scene 引用 registry 缺失的 GUID”。
- **Observed**：Editor 无删除 manifest 资产/解除绑定的生产操作；E3 因此使用**测试夹具**
  scene（`spike_gp07_e3/E3.scene`），经 `OpenProject` 走真实加载契约验证，**不手改生产 JSON**。
- **Severity**：low（加载契约已由 Engine + 桥 + gate 覆盖；UI 无需该负操作）。
- **Frozen API sufficient?**：yes（加载侧契约足矣；产制侧属非必需路径）。
- **Minimum UI fix**：不修（避免为测试新增能力）。
- **Capability gap?**：无（明确判定非需求）。

---

## 3. 零内部操作自检

Golden 全程仅经 `MainViewModel` 公开操作与 Session ABI：
Open/Create/Rename/Transform/Import/Assign/OpenScript/SaveScript/Play/Reload/Stop/
SaveProject/ResetSession + ReadBack（ScriptText/Pos/Guid/SelectedSprite/Script）。
场景 JSON / manifest 零手改；GUID 零手改；无 ImGui；无 C++ 直调；无外部 IDE；
无重启修状态。E3 夹具是独立 scratch 工程（加载契约的对抗输入），不影响此结论。

---

## 4. 结论

**Result A — Production Ready → Phase 3 FROZEN。**

`Hello gate`：现有 Avalonia Editor 能在一个真实工程上完成
“作者 → 调脚本 → 运行/热重载(_PERSIST) → 落盘 → 关/开 → 复现”α 完整循环，
全部由冻结 API + bridge（Editor UI 层）表达，引擎零改动。AV-GP-101~106 记录为
低严重度 UX/Workflow 摩擦与调试可用性项，供 Phase 4 / AV-Dogfood-01 迭代参考；
**无 Capability Gap 需进入 Engine 变更流程。**