# Avalonia Phase 3-D — Script Editor 正式迁移 + 运行时（Reload / _PERSIST / 错误恢复）Charter

> 建立日期：2026-08-28 · 分支：avalonia · 前置：P3-A ✅ P3-B ✅ P3-C ✅
> （gate3a/gate3b/gate3c ALL GREEN）
> 纪律：**先 Charter + Gate，再实现**；C++/Engine **零改动**（复用已验收的
> `ScriptInstance` S3/S4/S5），bridge 层内自足完成运行时。

---

## 1. 目标

P3-D 不是"把 Script Editor 搬过来"，而是验证脚本成为 **生产资产** 的完整闭环：

```text
Asset Browser                              Editor                     Runtime (Play)
  双击 game.lua  ─────────────────────→  Script Editor                 │
         ↓ 读取真实磁盘                        ↓ Save                     ↓
    UTF-8 / 大文件 / 不截断           改动 → DIRTY → Save             Play 克隆场景
         ↓                                ↓ 切换不丢                     ↓
   名称明确显示 / 空态                 Close 未保存提示             Reload 保留 _PERSIST
                                                                         ↓
                                                              Lua 错误诊断 / 不崩 / 修复恢复
```

**核心 Golden**：`Save → Play → 改 gameplay → Reload → 新行为生效` 且 `_PERSIST`
跨 Reload 保留；错误 Lua 不崩溃、可修复恢复。

**验证主线**（D1–D5）：读磁盘 → Dirty/Switch → Save+Reload(_PERSIST) → 错误恢复 → 磁盘一致。

---

## 2. 范围（D1–D5）

| 子项 | 内容 | 验收 |
|------|------|------|
| D1 | **Asset Browser → Script Editor**：双击 `.lua` → 读取真实磁盘内容；UTF-8 正常；大文件（>100KB）不静默截断；当前脚本名称显示；无脚本时空态 | 磁盘字节往返一致 + 空态 + 命名 + UTF-8 回读四断言 |
| D2 | **编辑 + Dirty**：修改 → Dirty；Save → 清除；切换实体/面板不丢缓冲；Close 未保存提示（UI 层） | dirty/save-clean/buffer-intact |
| D3 | **Save + Reload**：Play → 改 gameplay → Save → Reload → 运行健康；**`_PERSIST` 跨 Reload 保留**（gold：1→2） | `RuntimePersistInt` 探针证明 S4 |
| D4 | **Lua 错误恢复**：语法错误 Save → Play 失败（错误显示、不崩）；修复 → 恢复；运行中改成错误 → Reload 失败不崩 → 修复 → 恢复 | error surfaced + session 存活 |
| D5 | **持久化**：Edit → Save → Close → Reopen → **磁盘逐字节一致**（不比较 UI 文本） | 磁盘 UTF-8 字节往返 |

### ABI 增量（bridge 层，引擎零改动）

复用已验收的 `ScriptInstance`（Initialize/Reload/参数预算/S5 隔离）与
`GameplayAPI`/`ScriptAPI`（Engine.* 域名、`_PERSIST` S4 协议）。bridge 仅暴露：

```text
EditorSession_Play(assetIndex)          → 克隆编辑场景 → 绑定 GameplayAPI → ScriptInstance::Initialize → OnCreate
EditorSession_Reload()                  → m_Inst.Reload()（保留 _PERSIST）→ OnCreate
EditorSession_Stop()                    → OnDestroy + Shutdown + 场景回切编辑态
EditorSession_RuntimeTick(dt)           → OnUpdate（门可无头推进）
EditorSession_IsPlaying()
EditorSession_GetRuntimeError()         → Lua load/run 诊断文本（D4）
EditorSession_RuntimePersistInt(key,def) → `_PERSIST[key]` 运行态探针（D3b gold）
+ 事件 EV_PLAY_STARTED / EV_PLAY_STOPPED
```

**AV-GP 判定**：全部可由冻结 API（`ScriptInstance` + `ScriptAPI` + `GameplayAPI` +
`CaptureScene`/`InstantiateScene`）组合表达 → UI + bridge 自己解决，**无引擎改动**。

---

## 3. AV-G3-D Gate（`AvaloniaEditor --gate3d`）

### 3.1 主线（D1–D5 全断言，scratch 隔离运行）

```text
Open GP01 (10 entities / 33 assets)
→ (空态) 无选中脚本
→ 双击 game.lua → ScriptTitle 显示 / 内容 == 磁盘字节 (12626B, UTF-8)
→ 写入多字节中文+emoji → Save → 磁盘含 UTF-8 / 读回一致
→ 追加 ~192KB → Save → Reopen → UTF-8 字节往返一致（不截断）
→ 修改 → ScriptDirty -> Save -> clean
→ 边编辑边切换实体选择 → 缓冲未丢
→ Play(game.lua) -> 改 gameplay 参数 -> Save -> Reload -> 仍运行
→ _PERSIST 探针：Play 后 n=1 -> Reload 后 n=2（gold）
→ 错误 Lua -> Play 失败 / ScriptError 非空 / 不崩 -> 修复 -> Play 恢复
→ 运行中改成语法错误 -> Reload 失败 / Session 存活 -> 修复 -> Reload 恢复
→ Edit -> Save -> Close(ResetSession) -> Reopen -> 磁盘逐字节一致
```

### 3.2 运行态纪律（镜像 GP01ProductionSession / GP-DX-004 家族）

- Play 在**克隆的运行场景**上执行（编辑场景不被脚本污染）；
- 运行态编辑（Transform/Rename/Delete/Assign）仍被拒绝（edit-state only）；
- Reload 失败走 `ScriptInstance::Reload` 内部 pcall —— **Session 不崩溃**，
  错误经 `GetRuntimeError`/`ScriptError` 回显到 Console 与 Script Editor。

---

## 4. 晋升规则（延续 Phase 3）

- ✅ 已有 ABI 能力（`ScriptInstance` 等）的 Avalonia 运行时接线：Play/Reload/Stop/诊断。
- ❌ 为 UI/运行时完整而新增 Engine API；阻塞则登记 AV-GP-xxx 再走流程。
- 语法高亮 / 补全 / 断点调试 / 多脚本并行：**无需求证据，不做**（假能力，GP-DX-007 教训）。

---

## 5. 验收门槛

- `AvaloniaEditor --gate3d`：**ALL GREEN exit=0**（D1–D5 全断言）；
- `test_bridge` 新增 Runtime 三用例（Play/Reload/Persist/Stop、Director+Tick、
  Lua 错误恢复），全量 `EditorBridgeTest.*` **20/20 GREEN**（ASan 开启）；
- `test_scripting` **23/23 GREEN**（S3/S4/S5 依赖未动摇）；
- gate1/2/3a/3b/3c **回归 GREEN**（exit=0）；
- C++/Engine 零改动；ImGui 零；JSON 零手改。

---

## 6. 证据

| 证据 | 位置 |
|------|------|
| P3-D Gate ALL GREEN | `editor_avalonia/selftest.log`（`[P3-D GATE] ALL GREEN in 4009 ms` / `gate3d exit=0`） |
| 运行时契约测试 | `tests/test_bridge/test_editor_capi.cpp`（Runtime 三用例，20/20 PASS） |
| `_PERSIST` gold | `RuntimePlayReloadPersistStop`（1→2）＋ gate D3b 断言 |
| 错误恢复 | `RuntimeLuaErrorRecovery` ＋ gate D4/D4b 断言 |
| 回归 | gate1/2/3a/3b/3c exit=0；test_scripting 23/23 |