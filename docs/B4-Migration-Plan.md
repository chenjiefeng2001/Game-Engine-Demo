# B4 Migration Plan（ECS 显式 registration）

> **状态**: **已实施并验证**（`89d58dc` + `734a9bd`，`test_ecs` 21/21）
> **上游**: `docs/ADR-B4-ECS-Registration.md` §0.5（Accepted, 2026-10-04）
> **范围**: 仅 B4。Audio 另立，且需先做 design freeze
> **本文档不是 ADR**，只记录执行顺序与验收条件

**实施结果**：`InitializeComponentRegistry()` 落在 `ComponentRegistry.h/.cpp`（clean、已提交、与 HRC-3 无重叠），5 处 static-init registration 全部改写并补齐 `Joint3DComponent`；调用点为三个构造 `EntityManager` 的已提交启动路径（ECSTest / BackendTest / Rendering3DTest）。三配置 full build `0 error`、full suite `16/16`、`test_ecs` 21/21，location regression 与 `TenThousandEntities` 均保留通过。

---

## 1. 已接受的目标

ECS 采用**显式、受控 registration**：组件类型在使用前必须完成 registration，由明确的初始化边界执行。

**禁止**：自动注册、依赖 C++ static initialization order。

已完成的 runtime safety 层（`629ea47`）保持不变：registration 缺失 → 可诊断失败，而非首次使用时偷偷注册。

---

## 2. B4-1：bootstrap boundary

**定义位置**（均为已提交的 clean 文件）：

| 角色 | 位置 |
|---|---|
声明 | `engine/include/Engine/Core/ECS/ComponentRegistry.h` |
实现 | `engine/src/Core/ECS/ComponentRegistry.cpp` |
入口名 | `Engine::InitializeComponentRegistry()` |

要求：**幂等**（可重复调用）、**不引入自动注册**、**不依赖 static initialization order**。

### 2.1 owner 选择的只读确认结论

| 候选 | 结论 | 依据 |
|---|---|---|
`Application` / `SubsystemManager` | **排除** | 二者均为 HRC-3 未提交改动；在此注册等于让 B4 依赖 HRC-3，违反已接受裁决 |
函数内 static 初始化 lambda | **排除** | 即当前 `PhysicsComponents.cpp:16` 的形态，属被禁止的自动注册 |
逐个 `main()` 注册 | **排除** | 全仓 36 个入口，但仅 3 个 sandbox + 1 个测试真正构造 `EntityManager` |
ECS 自有边界 + 仅调用方枚举 | **采纳** | 全部相关文件 clean，无 HRC 依赖，调用点可枚举 |

### 2.2 调用点（全部已提交、clean）

| 调用点 | 形式 |
|---|---|
`sandbox/src/ECSTest` | `ECSTest::Run()` 开头 |
`sandbox/src/BackendTest` | `BackendTest::Run()` 开头 |
`sandbox/src/Rendering3DTest/main.cpp` | `test.RunAll()` 之前 |
`tests/test_ecs` | 保留 fixture 显式注册测试自有类型；另加边界测试 |

`RegisterComponentType<T>()` 本身已幂等（内部检查既有 meta）。

---

## 3. 迁移面（有界、可枚举）

| 项 | 数量 | 位置 |
|---|---|---|
static-init registrations 改写 | 5 | `PhysicsComponents.cpp:17-21` |
真实未注册生产类型补齐 | 1 | `Joint3DComponent`（`PhysicsComponents.h:112` 定义） |
错误注释修正（声称 static init 自动注册） | 5 | `ComponentRegistry.h:7`、`PhysicsComponents.cpp:3`、`:5` |

### 3.1 一处影响评估（实施后修正）

`PhysicsSyncSystem.cpp:59` 取 `Joint3DComponent` 并在 `:61` 正确判空。由于该类型从未注册，该判空**恒为假**，joint 清理逻辑实际从未执行。注册后清理会开始生效。

**但这是潜在而非活跃的行为变化**：实施期只读核查确认 `PhysicsSyncSystem` 在全仓**没有任何构造点**（0 处实例化），因此该路径当前不可达。注册 `Joint3DComponent` 只是消除一个潜伏缺口，不改变任何现行运行行为。

### 3.2 边界调用点的必要性验证

全仓 `AddComponent<...>` 使用这 5 个内置组件的位置**只有** `sandbox/src/ECSTest/ECSTest.cpp:271/277/282`。因此 §2.2 中只有 ECSTest 是当前必需的调用点；另两个（BackendTest、Rendering3DTest）是为其自身构造 `EntityManager` 的路径预置边界，避免将来新增 ECS 用法时再次依赖隐式注册。

没有任何测试文件使用这些内置组件（0 命中），故移除 static-init 不会使 `16/16` 退步。

---

## 4. 明确不做

- ECS / GameObject 两套模型收敛
- 生成式 registration 辅助
- 任何对 HRC-3 的依赖，或触碰 `Application.cpp` / `Time.cpp` / `EngineHost.cpp`
- 新增 ECS error API（沿用 `Log::ErrorLoc` 与 `HasComponent<T>()`）
- 逐个 `main()` 铺开注册
- 改动 `629ea47` 已确立的 runtime safety 语义

---

## 5. 执行顺序

1. **B4-1**：定义 `InitializeComponentRegistry()`（幂等）
2. 迁移 5 处 static-init registration + 补齐 `Joint3DComponent`
3. 在 §2.2 的调用点接入边界
4. tests / cleanup：修正 5 处错误注释，补边界测试
5. 三配置全量回归

---

## 6. 验收

三配置 `Debug` / `Release` / `RelWithDebInfo`：

- full build `0 error`
- full CTest `16/16`
- `test_ecs` `18/18`
- 保留 ECS location regression tests 与 `TenThousandEntities`

---

## 7. 提交边界

目标为**约一个 migration commit + 一个 test/cleanup commit**。

若实际需要第三个提交，必须是因为**证据表明边界需要拆分**，而不是把 cleanup 越做越大。

HRC-3 工作区全程隔离，不进入任何提交。