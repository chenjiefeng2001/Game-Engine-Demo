# Avalonia Editor Migration — Phase 2 Closure

> 冻结日期：2026-08-28 · 分支：avalonia · 依据：`docs/Avalonia-Editor-Migration-v1.md`
> 冻结裁定：**Phase 2 (State / VM / ABI) FROZEN** — AV-G2 全链路证据密封于此。
> 上一阶段：Phase 1 FROZEN（commit `c16b327`，锚 `avalonia-phase0-av001`）。

---

## 1. 冻结范围

Phase 2 交付 = **Editor Core 状态层**：把 Phase 1 的 Shell 提升为可完成
"打开工程 → 编辑实体 → 分配资产 → 改脚本 → 保存 → 重开全恢复" 的生产回路。

| 子项 | 内容 | 证据 |
|------|------|------|
| P2-1 ABI batch | Delete/Rename/AssignSprite/ScriptRead/ScriptSave/GetAssetGuid/GetEntitySprite/IsDirty + 4 新事件 | test_bridge 13/13 |
| P2-2 VM/View batch | Hierarchy 回路 / Inspector sprite / Asset assign / Script Editor / Dirty 徽标 / Close 提示 | AV-G2 34/34 |
| AV-G2 Golden Workflow | 冷启动→open→edit→rename→assign→create→script→save→reopen 全恢复 | ALL GREEN 2.1s |
| 回归 | Gate 1 / Integrity Gate | 两者全绿 |

**C++/Engine 零改动**（bridge 层内自足）；ImGui 零；JSON 零手改。

---

## 2. 密封证据

### 2.1 AV-G2 Golden Workflow（34 assertions，2.1s）

```text
open GP01                    -> hierarchy 10 / assets 33 / clean        PASS
select Player -> edit z 4→9  -> transform applied / dirty               PASS
rename Player → Hero         -> selection kept + new name shown         PASS
assign sprite -> Hero        -> inspector shows path / dirty            PASS
create NewProp               -> hierarchy +1 / visible / edit transform PASS
script edit (append+save)    -> loaded 11504 chars / dirty / save       PASS
save project                 -> clean                                   PASS
close → reopen               -> 11 entities                             PASS
Hero.z == 9.0 (edit survived)                                          PASS
sprite binding persisted                                                PASS
NewProp transform persisted                                             PASS
script audit line persisted                                            PASS
reopen clean                                                            PASS
```

### 2.2 test_bridge 13/13（ASan 全程开启）

| 契约 | 断言重点 |
|------|---------|
| DeleteEntityAndEvent | 删除生效 + 越界拒绝 + EV_ENTITY_DELETED |
| RenameEntityRules | 改名生效 + 空名/越界拒绝 + 失败不写 |
| AssignSpriteTwiceIdempotentTexture | 分配生效 + 幂等 + 非 Texture 拒绝 |
| ScriptReadWriteViaRegistry | 读→改→写→读回一致 + 非脚本拒绝 |
| DirtyStateContract | Clean→Edit→Dirty→Save→Clean 全环 |

（另含 Phase 0/1 既有 8 契约：生命周期/NullHandle/round-trip/实体创建/错误通道/未开工程拒绝/TransformRoundTrip/AssetQueryContract）

### 2.3 回归

- `AvaloniaEditor --gate1`：ALL GREEN（Phase 1 未回归）
- `tools/integrity_gate.ps1`：**ALL GREEN 122/122**（I1+I2+I3）

---

## 3. 封存教训：ASan UAF — ABI 边界工程第一课

### 3.1 事实

`ContentRegistry::GetAllEntries()` **按值返回** `std::vector<AssetEntry>`（临时对象）。
bridge 中 `const auto& entry = m_Reg.GetAllEntries()[i]` 把引用绑到临时 vector 的元素上，
语句结束临时即销毁 → 引用悬垂。`AssignSprite` 首次合约测试即被 ASan 捕获
（heap-use-after-free, EditorSession.cpp:214）；`ScriptRead`/`ScriptSave` 同构。

### 3.2 修复

按值拷贝条目后再使用：

```cpp
// 注意：GetAllEntries() 按值返回临时 vector，引用会悬垂（ASan 捕获），
// 必须按值拷贝条目再使用。
const auto entry = m_Reg.GetAllEntries()[static_cast<size_t>(assetIndex)];
```

### 3.3 固化为契约级原则

> **ABI contract implementation must not retain references into temporary
> ContentRegistry snapshots.** `GetAllEntries()` is a by-value snapshot;
> hold the copy, never a reference into it.

此原则写为 Phase 2 铁律：**任何经 `GetAllEntries()` 取条目的 ABI 实现必须按值持有**。
同类陷阱（按值返回容器的临时元素引用）在 bridge 层全量排查并修复。

### 3.4 测试侧配套修复

1. `EXPECT_EQ(a, strlen(buf))` 两实参求值顺序未定义 → 先取回返回值再断言；
2. scratch CWD 切换后路径须用相对 `assets/gp01/...`（与运行时 CWD 解析语义一致）；
3. `game.lua` 12.6KB > 4KB 缓冲 → `cap-1` 截断语义需 16KB 缓冲 + `ASSERT_LT` 守卫。

---

## 4. 当前未解决问题（Phase 3 及以后）

| 编号 | 问题 | 状态 |
|------|------|------|
| U1 | Viewport 仍为占位（Texture presentation 属 Phase 7，AV-003 DECIDED） | 不阻塞 |
| U2 | Play/Stop 运行时轨道（m_Playing 占位；SetEntityPosition 等 edit-state 拒绝已就位） | Phase 5 |
| U3 | 多场景/工程切换的自动恢复（GP-DX-001 OBSERVE） | 无真实场景证据 |
| U4 | Undo/Redo（DF08 裁定非 P0） | 不进 Phase 3 |
| U5 | Console 面板当前为 VM 内 LogToConsole 直通，未验证 G2 log bridge 在 Avalonia 下链路 | Phase 3-E |
| U6 | Asset Browser 无搜索/过滤 | Phase 3-C |

---

## 5. Phase 3 边界

Phase 3 = **Production Panels**（Avalonia 承担完整日常内容生产流程），
五个面板 + 一个 Golden Gate：

```text
P3-A Hierarchy      ← selection/context 上游，先做
P3-B Inspector      ← 可扩展架构（Component→ViewModel→View），不做反射 Inspector
P3-C Asset Browser  ← Discovery/Selection；GUID 管理留在 ContentRegistry
P3-D Script Editor  ← 生产级编辑（文件名/Dirty/Save/Reload/错误显示），不做 IDE
P3-E Console        ← 验证 Engine LOG→Session→Event→Avalonia 链路，再谈增强
P3-F AV-G3 Production Authoring Gate（完整生产循环）
```

**边界红线**：

- **不碰 Viewport**（Phase 7：C++ GPU → FBO/Texture → GPU interop → Avalonia rendering，
  属渲染架构迁移量级，隔离正确）；
- **不新增 Engine API 仅为 UI 完整**；能力缺口按 AV-GP-xxx 流程（见 Phase 3 Charter）；
- Hierarchy 第一版不做复杂树结构（无 parent/reparent 需求证据）。

---

## 6. 版本锚

- 冻结 tag：`avalonia-phase2-av-g2`
- 上一锚点：`avalonia-phase0-av001`（Phase 0/1）
- 下一阶段 Charter：`docs/Avalonia-Phase3-Charter.md`
