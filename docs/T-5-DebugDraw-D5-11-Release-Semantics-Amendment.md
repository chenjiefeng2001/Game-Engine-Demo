# T-5 D5-11 — Release Semantics Amendment

**状态**：`ACCEPTED — CONTRACT AMENDMENT`
**日期**：2026-10-31
**上位契约**：`docs/T-5-DebugDraw-GateA-Contract.md`
**裁决输入**：用户 2026-10-31 分支裁决

---

## 1. 原始决策（保留历史）

原 D5-11：

> 通用 DebugDraw 在 Debug/Release 均可用；Jolt 物理适配保持 Debug-only，不得在 Release 假称提供物理线框能力。

该措辞形成于 Gate A，基于当时对 `JPH_DEBUG_RENDERER` 配置的假设。

---

## 2. 修订后的 D5-11

> **D5-11 — Release Semantics（修订）**
>
> 通用 DebugDraw 服务在 Debug 和 Release 配置均可用。
>
> Jolt 物理 DebugDraw 适配能力由 `JPH_DEBUG_RENDERER` 编译配置决定，不再规定为 Debug-only。
>
> 当前项目启用 `DEBUG_RENDERER_IN_DEBUG_AND_RELEASE=ON`，因此 Jolt 物理 DebugDraw 适配在 Debug 和 Release 均可用。
>
> 适配器可用不代表 Jolt DebugRenderer 的全部功能均已实现。残余功能和限制必须单独记录，不得推断为完整支持。

---

## 3. 依据

- Jolt 构建选项 `DEBUG_RENDERER_IN_DEBUG_AND_RELEASE=ON`。
- `JPH_DEBUG_RENDERER` 通过 Jolt target 传播到 `EngineCore` Release 构建；`build/engine/EngineCore.vcxproj` Release 包含该宏。
- D3 的 `test_debugdraw` Jolt 形状门禁在 Debug / Release 均通过（22/22）。
- 本修订不改变任何构建选项或源码行为；它使契约文字与已实测构建事实一致。

---

## 4. 边界

- 不把“适配器可用”扩展为“Jolt DebugRenderer 全部能力已实现”。
- `DrawText3D`、LOD camera position、solid 模式等残余能力见 `docs/T-5-DebugDraw-Jolt-DebugRenderer-Residual-Capabilities.md`。
- 生产 EngineHost/Bridge/Editor 接线仍未授权。
- 本修订不重新打开 T-5 v1 范围，不改变 Gate B/C/D1/D3 的实现结论。

---

## 5. 历史与引用

- 原 D5-11 文本保留在 `docs/T-5-DebugDraw-GateA-Contract.md` 的 D5-11 行；该行标注本修订。
- Gate A §6.1 的构建观察保留，并指向本修订。
- D3 记录引用本修订；最终关闭记录保留本修订与残余限制。
