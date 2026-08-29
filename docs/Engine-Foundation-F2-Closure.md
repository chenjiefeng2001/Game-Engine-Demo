# Engine Foundation v1 — F2-A Closure（Collider Contract 声明式上卷）

> 建立日期：2026-08-28 · 上游：`Engine-Foundation-F2-Charter.md` + `Engine-Foundation-Phase-Plan.md` §F2
> + `Engine-Foundation-F1-Closure.md`（模板与晋升门槛）+ `Component-Ledger.md` F2 Ledger
> 提交：`Engine Foundation v1: establish Collider component contract (F2-A)`
> 标签：`engine-foundation-v1-f2-a-collider`（F2-A 不可变证据锚点）
> 状态：**FROZEN**。F2-A 封存；F2-B（Physics Binding）未开始，任何 F2-B 代码不得进入本提交之后的工作区历史之前先单独立项。

---

## 0. 一句话结论（防假闭环）

> **F2-A ≠ Collider 可用。F2-A 只证明 Collider 是一个可持久化、可反射、可脚本访问的声明式契约。**
>
> Collider 在真实 Physics 运行时中「存在、同步、可重建、行为一致」——**一个都还没有证明**。
> 后续任何进展汇报，凡涉及 Physics 能力，必须以 F2-B/F2-F 的实测证据为准，不得以「API 有了」代替。

---

## 1. F2-A 目标与结果

F2-A 是 F2（Collider 实证）的第一阶段：只做**声明式上卷**，把 F1 的
`Contract → Registry → Reflection → Serialization → Lua` 模板在第二个组件（Collider）上复现，
证明 **F1 模板不是 Camera 特例、可跨 Physics/ECS 边界**。

**结果达成**：

- `ColliderComponent.{h,cpp}` —— 纯声明式契约组件，**零 Physics 依赖**
  （`#include` 仅 `Component.h`；不 #include PhysicsDefs.h / Jolt / ECS；不持有任何
  Physics 内部对象：Jolt BodyID · ShapeRef · ECS Entity ID · world 指针 · 帧局部句柄）。
- `ColliderShape`（Circle/Box）以**稳定字符串身份**表达，持久化 / Lua / Editor / 未来 Adapter 共用。
- `RegisterColliderComponent()` 收编入 `ComponentRegistryGo::EnsureRegistered`（F1 注册栈 +Collider），
  稳定类型名 `"Collider"` 与 Camera 同源。
- 反射 8 属性四类型全覆盖（Bool/String/Float/Int）：`enabled / shape / radius / halfX / halfY /
  isSensor / category / mask`。
- Lua **只走通用 `Engine.component.*`**，零新增特化域（无 `Engine.collider.*` / `Engine.physics.*`）。
- 多字段结构化序列化经 `SceneSerializerV1` components[] 往返，冷重启全保真。

## 2. 验收证据：174/174 PASS（Integrity Gate ALL GREEN）

| Target | 结果 | Target | 结果 |
|--------|------|--------|------|
| test_bridge    | **28/28**（+F2×3） | test_physics | 16/16 |
| test_scripting | **27/27**（+F2E/F2C） | test_renderer | 9/9 |
| test_core      | 20/20 | test_job | 4/4 |
| test_content   | 42/42 | test_e2e | 6/6 |
| test_gp01      | 22/22 | **合计** | **174/174** |

`tools/integrity_gate.ps1` → **ALL GREEN（I1+I2+I3）**，in-scope total = 174（MSVC Debug + ASan env）。

新增 F2-A 契约测试：
- `test_bridge` — `F2ColliderContractLifecycle`（挂载/幂等/默认值/多字段多类型写读/移除）/
  `F2ColliderSaveLoadColdRestart`（落盘 JSON 证据 → 冷重启全保真）/ `F2ColliderNegativePaths`
  （未知类型/越界/坏值拒绝且不改值）
- `test_scripting` — `F2E_ComponentContract_LuaCollider`（通用 `Engine.component.*` 读写 Collider，
  含未知形状/未知类型拒绝）/ `F2C_LuaCollider_ReflectedOnGameObject`（Lua 创建落于真实 GameObject 组件）

## 3. 五线证据对账

| 线 | F2-A 交付 | 证据 |
|----|----------|------|
| Contract / Registry | `ColliderComponent`（声明式状态，零 Physics 依赖）+ `RegisterColliderComponent` 收编 F1 注册栈 | `ColliderComponent.{h,cpp}`、`ComponentRegistry_Go.cpp` |
| Runtime | 仅契约状态本体；**未接任何 Physics Runtime**（F2-B） | 无 Runtime 侧改动（防假闭环） |
| Serialization | `Serialize/Deserialize` 经 components[] 往返；未知类型告警 + 实体保留沿用 F1 | `F2ColliderSaveLoadColdRestart` |
| Lua | 通用 `Engine.component.*` 四类型读写，零新增特化域 | `F2E` / `F2C` |
| Editor / Avalonia | **本阶段未做**（Inspector 行为属 F2-E） | 无 bridge/UI 改动（防范围蔓延） |

## 4. F2 十栅栏对账（F2-A 只闭环声明式部分）

| # | 栅栏 | F2-A 状态 |
|---|------|----------|
| 1 | 新建 Collider | ✅ 契约测试覆盖 |
| 2 | Inspector 修改 → 进入 Physics | ⏸ **F2-E**（本阶段未做） |
| 3 | Physics 实际使用 | ⏸ **F2-B+F2-F**（未做，避免假闭环挂真 Physics） |
| 4 | Save | ✅ 落盘 JSON 证据 |
| 5 | 冷启动 | ✅ 冷重启全保真 |
| 6 | Load | ✅ 还原后参数一致 |
| 7 | Collider 参数一致 | ✅ 声明式一致 |
| 8 | Physics 行为一致 | ⏸ **F2-F**（未做） |
| 9 | Remove 后行为消失 | ⏸ **F2-B+F2-F**（声明式移除已测；Physics 侧未测） |
| 10 | 未知类型不破坏场景 | ✅ 沿用 F1 未知类型告警 + 实体保留 |

## 5. 门表：已证明 / 尚未证明

| 门 | 状态 |
|----|------|
| Collider Contract / Registry | ✅ |
| Serialization / Cold Restart | ✅ |
| Lua 通用 Component API | ✅ |
| Physics 实际绑定 | ⏸ **F2-B** |
| Runtime 生命周期所有权 | ⏸ **F2-B** |
| Destroy Physics World → 重建 | ⏸ **F2-B** |
| Inspector Physics 行为 | ⏸ **F2-E** |
| Collision 行为一致性 | ⏸ **F2-F** |

## 6. 明确排除项（避免向后门夹带 / 范围蔓延）

- **`PhysicsColliderAdapter`**：F2-A 工作区**不存在任何实现**（仅 Charter/头注释提及为 F2-B 计划）。
  若后续提交中出现它，即违反本封存边界。
- **`engine/src/Core/Application.cpp` 启动循环退出点日志**（GP1-DX 调查遗留）：**不纳入本提交**，与 F2 无关。
- **spike_ 目录（gp06 / gp07 / gp07_e3）**：编辑器 spike 遗留 scratch，不纳入。
- **F2-B/F2-E/F2-F 任何预实现**（Physics ECS/Jolt 改动、bridge/UI 改动）：本提交零包含。
- RigidBody / Prefab / Collision Events API / 渲染视口 / RenderGraph：F2 红线，全程不做。

## 7. F2-A → F2-B 交接（F2-B 第一道硬门）

F2-B 的核心问题**不是「把 Collider 接上 Jolt」**，而是：

> **现有 Physics 架构能否在不污染 Component Contract 的情况下，为 Collider 提供明确、唯一、
> 可恢复的 Runtime Ownership？**

所有权模型（写死，F2-B 不得反转）：

```text
ColliderComponent  = 唯一声明事实源
Physics Adapter    = 唯一 Runtime 映射层
Physics Body       = Runtime 派生对象
```

F2-B 第一版只验证一条链：

```text
GameObject → ColliderComponent → PhysicsColliderAdapter → ShapeDef → Physics Body
```

F2-B 第一道硬门（破坏性测试，链不通即 Result C，视为成功）：

```text
Create Scene → Collider=Box → Create Physics Runtime → Body exists
→ Destroy Physics World → ColliderComponent 仍存在
→ Cold Load / Rebuild Physics → 重新创建 Body（参数完全来自 Component）
→ Physics 再激活 → 行为与第一次运行一致
```

若该链做不到：**不要修补到「看起来能跑」**，直接登记 Result C：
「Physics 架构当前无法满足 Component Foundation v1 的生命周期契约」。

## 8. 测试基础设施观察项（沿用 F1，不是引擎缺陷）

- **scratch 目录文件锁（顺序敏感）**：单跑 `*F2*` 等 scratch 用例时，async spdlog file sink
  持有的进程内首个日志句柄可能使 scratch 目录 `remove_all` 失败；**完整套件按文件顺序运行
  （首个日志在仓库根）全部通过**（本次 Gate 与两套件全量直跑均验证）。属后续基础设施项，非 F2 阻塞。
