# Engine Foundation v1 — F2 Charter（Collider 实证）

> 建立日期：2026-08-28 · 上游：`Engine-Foundation-v1.md` + `Engine-Foundation-Phase-Plan.md` §F2
> + `Engine-Foundation-F1-Closure.md`（§6 晋升门槛）+ `Component-Ledger.md` F2 Ledger
> Baseline：`7b8e703` / tag `engine-foundation-v1-f1-camera`
> 状态：**F2-0 已完成（Baseline Freeze）；F2-A 已完成（Collider Contract，声明式上卷）。**
> 下一步：**F2-B Physics Binding**（才写 Physics Adapter）。

---

## 1. F2 的核心问题（唯一裁决）

> **Camera 的四面向/五线组件模板，能否成为真正通用的 Engine Foundation Contract，而不是 Camera 特例？**

Collider 是合适的验证对象：它跨到 Physics/ECS 边界，且需要 Camera 未覆盖的
「多字段结构化序列化 + 运行时同步 + 与既有物理系统真实互操作」。

## 2. F2 — 实施阶段

| 阶段 | 内容 | 完成判据 |
|------|------|---------|
| **F2-0 Baseline Freeze** | 从 `engine-foundation-v1-f1-camera` 出发，跑完整 Integrity Gate，登记 Ledger（EF-F2-001 / GP-F2-001），写本 Charter。不允许顺手修其他问题 | ✅ Gate ALL GREEN（I1+I2+I3, in-scope 169）；Ledger + Charter 落盘 |
| **F2-A Collider Contract** | 定义稳定的 Collider 契约：shape/type、radius / half-extents 等必要参数、enabled、collision layer/mask、稳定类型名、Registry factory、Reflection metadata。**不暴露 Physics 内部结构给 Contract** | ✅ 契约头 `ColliderComponent` + `ColliderShape`(稳定串) + 注册 `Collider` + 反射（Bool/String/Float/Int 四类型、8 属性），**零 Physics 依赖** |
| **F2-B Physics Binding** | `GameObject → ColliderComponent → Physics/ECS representation`。明确生命周期所有权与同步责任：Create / Attach / Modify / Remove / Destroy / Reload 各发生什么。**不出现 Camera 式的假闭环**（真实系统旁边另有一套对象） | 绑定后 Physics 数据可写回 / 读回，ownership 唯一 |
| **F2-C Serialization** | Create → Save → Cold Restart → Load → **Physics representation 真正还原**。不止检查 JSON 有 `components[]`，要验证重载后 Physics 实际用的是这个 Collider | JSON schema + 还原后 Physics 行为一致 |
| **F2-D Lua** | 只暴露通用 `Engine.component.*`（add/has/get/set/remove）。**不创造 `Engine.physics.set_radius` / `Engine.collider.set_shape`** | 无新增特化域 |
| **F2-E Avalonia** | Inspector 至少：查看 Collider、修改核心参数、Add / Remove、Dirty、Save；且**编辑器修改真实进入 Physics**（不只改 UI/JSON） | 修改→Physics 可观测 |
| **F2-F 真实互操作** | Collider + 现有 Physics → 移动/接触/阻挡 → Save → Cold Restart → Load → 继续运行 | 端到端行为一致（Camera-only 阶段不具备） |

## 3. F2 的 Gate（验收只接受此闭环）

> **Contract → Registry → Reflection → Runtime → Physics → Serialization → Lua → Avalonia → Cold Restart → Physics Re-activation**

必须覆盖：
1. 新建 Collider
2. Inspector 修改
3. Physics 实际使用
4. Save
5. 冷启动
6. Load
7. Collider 参数一致
8. Physics 行为一致
9. Remove 后行为消失
10. 未知 component 类型不破坏整个场景

> F2-A 已固化的契约测试：`test_bridge` F2×3（生命周期 / 落盘冷重启 / 负路径）、
> `test_scripting` F2E/F2C（Lua 通用 `Engine.component.*` 读写 Collider + 落于真实 GameObject）。
> 按栅栏逐条对账：**F2-A 已验 1 新建 / 4 Save / 5 冷启动 / 6 Load / 7 参数一致 / 10 未知类型**（声明式）。
> **3-Physics 实际使用 / 8-Physics 行为一致 / 9-Remove 后行为消失 依赖 F2-B+F2-F 的 Physics Adapter
> 才最终闭环（本阶段未做，避免假闭环挂真 Physics）**。2-Inspector 修改进入 Physics 依赖 F2-E。

## 6A. F2-A 完成记录（Collider Contract）

- **交付**：`engine/.../ColliderComponent.{h,cpp}`（纯声明式，`#include` 仅 Component.h，**不含** PhysicsDefs/Jolt/ECS）；
  `RegisterColliderComponent()` 收编入 `ComponentRegistryGo::EnsureRegistered`（F1 注册栈 +Collider）。
- **契约字段**：`shape`(稳定字符串 Circle/Box) · `radius` · `halfX`/`halfY` · `isSensor`(bool) ·
  `category`/`mask`(uint16) · `enabled`。四反射类型全覆盖（Bool/String/Float/Int），多字段结构化序列化。
- **形状收敛**：F2 仅 Circle + Box（radius / halfExtents 为单值参数）；Edge/Chain/Polygon 属现有 Physics 能力，
  不在 F2 Collider 契约内（避免范围蔓延）。
- **所有权**：Component 拥有声明式状态；shape 以稳定串身份表达，**不映射 Physics 表示**（`ToShapeDef` 留待 Adapter）；
  世界位置归 Transform。符合 F2 裁定 §（Component 唯一所有者）。
- **证据**：`test_bridge` 28/28、`test_scripting` 27/27；引擎回归 test_core 20/20、test_content 42/42、
  test_gp01 22/22 全绿。Evidence-Baseline 升至 **174**。
- **下一步 F2-B**：只在此时写 PhysicsColliderAdapter（Component→ShapeDef/BodyDef 映射 + 生命周期所有权）。未做。

## 4. F2 特别不做什么

- ❌ RigidBody · ❌ Prefab · ❌ Camera 渲染视口 · ❌ RenderGraph
- ❌ Collision Events API · ❌ 大规模组件迁移 · ❌ ECS 重构 · ❌ Undo/Redo
- ❌ 为了“完整性”新增一批组件

> 若 Collider 自身暴露出双模型架构无法干净表达，这反而是 **F2 最有价值的结果**。

## 5. 最终裁决（三选一）

| Result | 含义 | 后续 |
|--------|------|------|
| **A — Promote** | Collider 完成完整闭环 → 证明 F1 模板可跨 Physics 边界 → 进入 RigidBody/其他组件模板化 | 批量迁移组件 |
| **B — Blocked** | 发现架构/生命周期问题，但可定义明确的最小 Foundation 修复 | 进 Ledger，不扩大范围 |
| **C — Contract Failure** | 现有双模型无法在不重构下形成可靠契约 → **停止继续补组件**，重处理 Engine Foundation 架构 | 停止补组件 |

> **Result C 视作成功。** F2 的判断对象是“引擎是否具备支撑未来项目的基础设施”，而非“引擎有没有 Collider”。

## 6. F2-0 Baseline Freeze 记录

- **起点**：`git rev-parse 7b8e703`（F1 提交）+ `engine-foundation-v1-f1-camera`。
- **Integrity Gate**：`tools/integrity_gate.ps1` → **ALL GREEN（I1+I2+I3），in-scope total = 169**
  - I1：全部 `tests/**/*Test.cpp` 已注册
  - I2：9 个 in-scope 目标计数与基线一致（test_bridge 25 / test_scripting 25 / … / 合计 169）
  - I3：DF01–DF09 + GP01 全部由已执行且通过的测试承载
- **工作区**：仅余 `Application.cpp`（GP1-DX 遗留，非 F2 范围）+ spike 目录（未跟踪 scratch），F2 不触碰。
- 本阶段零引擎改动（仅文档：Charter / Ledger / 基线记录），严守“先锁 baseline 再写 Collider”。

## 7. 验收证据将在哪些文件落盘（预期）

- `engine/.../ColliderComponent.{h,cpp}`、`ComponentRegistry_Go.cpp`（登记 `Collider`）
- `SceneSerializerV1` components[] + F2-C 往返测试
- `GameplayAPI.cpp` 无新增域（仅复用通用 `Engine.component.*`，必要时补反射能力）
- bridge `capi` + Avalonia inspector（F2-E）
- `test_bridge` / `test_content` / `test_scripting` 新增 F2 契约用例
- 本 Charter §3 十条栅栏以 gtest 固化