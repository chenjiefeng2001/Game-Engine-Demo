# Engine Foundation v1 — F1 Closure

> 建立日期：2026-08-28 · 上游：`Engine-Foundation-v1.md`（F0 审计）+ `Engine-Foundation-Phase-Plan.md` §F1
> 提交：`Engine Foundation v1: establish Camera component contract`
> 标签：`engine-foundation-v1-f1-camera`（可精确回退的“五线全通”基线锚点）
> 状态：**FROZEN**。F1 封存，后续 F2 以此为模板与回退基准。

---

## 1. F1 目标与结果

F1（Entity / Component Contract）以 **Camera 为实证对象**，目标是证明能一次性打通：

```
Component Contract → Registry → Reflection → Serialization → Lua → Editor/Avalonia → Contract Test
```

**结果达成**：Camera 成为矩阵中第一个「四面向（Runtime/Serialization/Scripting/Editor）全通、
九维全 ✅」的契约组件，F2 其余组件（Collider/RigidBody/Prefab）获得可复制的模板。

## 2. 验收证据：169/169 PASS

全部 9 个测试目标（MSVC Debug + ASan env）在 `engine-foundation-v1-f1-camera` 前全绿：

| Target | 结果 | Target | 结果 |
|--------|------|--------|------|
| test_bridge    | **25/25**（+F1×4） | test_physics | 16/16 |
| test_scripting | **25/25**（+F1E/F1C） | test_renderer | 9/9 |
| test_core      | 20/20 | test_job | 4/4 |
| test_content   | 42/42 | test_e2e | 6/6 |
| test_gp01      | 22/22 | **合计** | **169/169** |

补充：Avalonia 工程 `dotnet build` **0 Warning / 0 Error**。

新增 F1 契约测试：
- `test_bridge` — `F1CameraComponentLifecycle` / `F1ComponentContractNegativePaths` /
  `F1CameraSaveLoadColdRestart`（落盘→冷重启全保真）/ `F1UnknownComponentTypeLoadWarnsAndEntityRemains`
- `test_scripting` — `F1E_ComponentContract_LuaCamera`（通用 `Engine.component.*`）/
  `F1C_LuaCamera_ReflectedOnGameObject`（Lua 创建落于真实 GameObject 组件）

## 3. 五线证据对账

| 线 | 关键交付 | 证据 |
|----|---------|------|
| Contract / Registry | `ComponentRegistryGo`（稳定类型名→工厂，`Register/IsRegistered/Create/Count/EnsureRegistered`）；`Component.h` 反射元数据（`ComponentValueType`、`ComponentPropertyDesc/Value`、`GetProperty*`） | `ComponentRegistry_Go.{h,cpp}`、`Component.h` |
| Runtime | `GameObject::{Add/Get/Has/Remove}ComponentByName + Attach`（经 registry，非 typeid）；`CameraComponent`（zoom/viewport/bounds/enabled + `MakeOrthographic`） | `GameObject.{h,cpp}`、`CameraComponent.{h,cpp}` |
| Serialization | `SceneSerializerV1` 快照新增 `components[]`（type=稳定名 + data=组件 Serialize）；未知类型告警 + 实体保留。**组件字段并入 `SerializedEntity` 基类**，既有测试构造点零改动 | `SceneSerializerV1.{h,cpp}`、F1 落盘/冷重启测试 |
| Lua | `Engine.component.*` 通用域（add/has/get/remove + get/set number/bool/string，经反射元数据，不堆特化） | `GameplayAPI.cpp`（component 域）、F1E/F1C |
| Editor / Avalonia | bridge `AddComponent/RemoveComponent/HasComponent/GetComponentCount/GetComponentTypeAt/GetComponentProperty/SetComponentProperty` + `EV_COMPONENT_CHANGED`；Avalonia Camera inspector | `capi.{h,cpp}`、`EditorSession.{h,cpp}`、`EditorBridgeApi.cs`、`MainViewModel.cs`、`MainWindow.axaml(.cs)` |

设计原则（F1 定型，F2 沿用）：
- **契约身份 = 稳定字符串类型名**，与持久化 / Lua / Editor 三处共用，杜绝 typeid / class name / UI 字符串依赖。
- **序列化优先**：组件如何进快照、如何还原，先于任何单个业务组件（回 F0 红线 2）。
- **Reflection 在组件侧声明**（`GetProperty*`），Inspector / Lua 经元数据读写，不需知道 C++ class。
- **不堆 `Engine.<x>.*` 特化**：一律走通用 `Engine.component.*` + 反射属性。

## 4. 测试基础设施观察项（不是引擎缺陷）

> 纪律：以下为**测试基础设施 / 环境观察项**，**不得误登记为引擎功能缺陷**。

- **scratch 目录文件锁（顺序敏感）**：`--gtest_filter` 单独跑 `*F1*` 等 scratch 用例时，
  部分用例报 `remove_all: ...gate_p3 被占用`。根因是 async spdlog 的 file sink 持有
  「进程内首个日志所指向」的 `logs/engine.log` 句柄；该句柄在进程生命周期内不释放，
  导致 cwd 切到 scratch 后，后续用例对附着该日志的 scratch 目录 `remove_all` 失败。
  该现象**同样命中既有测试**（`RenameAssetKeepsGuidAndBinding` / `DirtyStateContract`），
  与 F1 无关；**完整套件按文件顺序运行时（首个日志在仓库根），全部通过**。
- **处置建议**：如想根治，应在测试 fixture 或 logger 侧让 file sink 随 cwd 切换重开/换名
  （如稳态路径下禁用文件落盘），或统一经 `tools/run_test.cmd` / CTest
  `ASAN_WIN_CONTINUE_ON_INTERCEPTION_FAILURE=1` 全量直跑。属后续基础设施项，非 F1 阻塞。

## 5. 明确排除项（避免向后门夹带 / 范围蔓延）

- **`engine/src/Core/Application.cpp` 的启动循环退出点日志**（GP1-DX 调查遗留）:
  **不纳入本提交**，与 F1 无关。
- **`MakeOrthographic` 接真实渲染视口**：属于 RenderGraph / Viewport 边界，当前 RenderGraph
  已明确隔离，除非真实产品需求证明其为 Foundation 阻塞，**本阶段及 F2 均不并入**。
- **spike_ 目录（gp06 / gp07 / gp07_e3）**：编辑器 spike 遗留 scratch，不属于 F1 契约实现，
  本提交不纳入。

## 6. F1 → F2 晋升门槛（下一阶段展望，暂不展开）

F2 不以「再补三个组件」为成功标准，而以 **「证明 F1 模板不是 Camera 特例」** 为门槛：

> Collider 作为第二个实证组件，必须完整通过：
> `Contract → Registry → Reflection → Runtime → Serialization → Lua → Editor → Save/Load → Cold Restart → Gate`

- 优先序：**Collider → RigidBody → Render/Visual → Prefab**。
- Collider 需额外验证 Camera 未覆盖的关键点：多字段结构化序列化、Physics/ECS 边界、
  Runtime ↔ Component contract、Inspector 参数编辑、与现有 Physics 系统的真实互操作。
- **防 F0 双模型回潮**：不得因「Collider 已能跑」即宣称组件体系完成；必须证明
  **GameObject Component 与实际 Physics/ECS 数据之间存在明确、稳定、可序列化的契约**。