# Avalonia Phase 3-C — Asset Browser 正式迁移 Charter

> 建立日期：2026-08-28 · 分支：avalonia · 依据：`docs/Avalonia-Phase3-Charter.md`
> 前置：P3-A（Hierarchy）✅、P3-B（Inspector）✅（gate3a/gate3b ALL GREEN）
> 纪律：**先 Charter + Gate，再实现**；C++/Engine 零改动，bridge 层内自足。

---

## 1. 目标

P3-C 不是"把旧 Asset Browser 搬过来"，而是验证 **Asset → Entity → Component**
的生产闭环：用户不理解 GUID / Registry / Resource Lifecycle 也能完成
"找资产 → 选中实体 → 分配 → 落盘 → 重开一致"。

**验证主线**：Discovery → Selection → Assign → Inspector 更新 → Dirty →
Save → Reopen → 全一致。

---

## 2. 范围（C1–C5）

| 子项 | 内容 | 验收 |
|------|------|------|
| C1 | **Asset Discovery**：All/Texture/Script 类型过滤 + 大小写不敏感搜索 + GUID/path/type 显示 + 空结果状态 + Registry 更新后自动刷新 | 过滤/搜索/空态/刷新四断言 |
| C2 | **Asset Assignment**：选中实体 → Assign Sprite / Assign Script → Inspector 更新 → Dirty | 与 P3-B 同一契约路径 |
| C3 | **Import UX**：File Dialog → Import → ContentRegistry → Asset Browser 自动刷新 → Assign | 导入后列表 +1、可分配、可保存 |
| C4 | **双击行为**：`.lua` → Script Editor；Texture → 选中/预览信息；其他 → 明确"不支持直接编辑" | 双击分派正确 |
| C5 | **持久化**：Import/Assign/Rename → Save → Reopen → GUID 一致 | AV-G3-C 全断言 |

### ABI 增量（bridge 层，引擎零改动）

`ContentRegistry` 已具备 `Import`（幂等）/`Unregister`/`RegisterExplicit`/
`SaveManifest`（`ContentAsset.h` 冻结契约）。P3-C 仅需在 bridge 暴露两个操作：

```text
EditorSession_ImportAsset(path, type)   → ContentRegistry::Import（幂等，返回资产索引）
EditorSession_RenameAsset(index, name)  → fs::rename 物理文件 + Unregister/RegisterExplicit
                                          （GUID 不变 → 场景绑定稳定）
+ 事件 EV_ASSET_IMPORTED / EV_ASSET_RENAMED
```

**AV-GP 判定**：全部可由冻结 API 组合表达 → UI 自己解决，无引擎改动。

---

## 3. AV-G3-C Gate（P3-C 收尾验收）

### 3.1 主线：Import → Assign → Save → Reopen 全一致

```text
Open GP01 (10 entities / 33 assets)
→ Select Player
→ Import texture（拷入 scratch）→ Assets 33→34
→ Assign Sprite → Inspector 显示该纹理路径 / Dirty
→ Import script → Assets 34→35
→ Assign Script → Inspector 显示该脚本路径 / Dirty
→ Save → Clean
→ Rename texture asset → Save（SaveManifest 落盘）
→ Close → Reopen
→ Player 的 sprite GUID == 保存前（两处 GUID 一致）
→ Player 的 script GUID == 保存前
→ Renamed asset 的新 path 出现在 Browser / Inspector / Scene 三方
```

### 3.2 DL-02 绑定不丢（P3-C 起正式加入）

```text
Create Entity
→ Assign Sprite → Assign Script
→ Save → Reopen
→ bindings intact（sprite + script 均恢复）
```

> DL-02：新增实体后，Asset/Inspector binding 不能静默丢失。

---

## 4. 晋升规则（延续 Phase 3）

- ✅ 已有 ABI 能力的 Avalonia UI 化（过滤/搜索/导入/分配/重命名）。
- ❌ 为 UI 完整而新增 Engine API；阻塞则登记 AV-GP-xxx 再走流程。
- 多选/拖拽/缩略图/依赖图：**无需求证据，不做**（旧面板这些是假能力，GP-DX-007 教训）。

---

## 5. 验收门槛

- AV-G3-C 主线 + DL-02 全断言 GREEN；
- test_bridge 新增 ImportAsset/RenameAsset 契约测试，全量回归 GREEN（ASan 开启）；
- gate1/2/3a/3b 回归 GREEN；
- C++/Engine 零改动；ImGui 零；JSON 零手改。
