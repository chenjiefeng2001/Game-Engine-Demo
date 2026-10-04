# ADR: B4 ECS 组件注册模型

> **状态**: 决策草案已提出（Proposed — 待接受）— §1–§10 仍为决策输入且不含结论；§0.5 记录决策方提交的草案
> **最后更新**: 2026-10-04
> **涉及范围**: `engine/include/Engine/Core/ECS/`、`engine/src/Core/ECS/`、`tests/test_ecs/`、`engine/include/Engine/Core/Scene/Serializer.h`
> **影响面**: 注册模型本身仍待接受；但其 runtime-safety 前置条件已关闭，`test_ecs` 已全绿（三配置均 16/16，18/18）

---

## 0. 本文档的定位

本文档**不是**架构批准记录，只做三件事：陈列**已验证事实**、列出**可行选项**、分析**每个选项的后果**。

**本文档刻意不给出推荐**，并且**不把任何注册方式写成技术事实** —— "应该显式注册" / "应该自动注册" 正是待决内容本身。

§0.5 记录的 Proposed Decision 由**决策方**提交，属方向性输入；§1–§10 的分析仍不含推荐，且 §0.5 **尚未被接受**。

---

## 0.5 Proposed Decision（草案，待接受）

> **Status: Proposed**
> 由决策方提交。**尚未被接受，不构成已批准架构**，也**不触发任何迁移实现**。
> 本节与 §1–§9 的关系：§1–§9 仍是决策输入且不含推荐；本节是叠加在其上的方向性输入。

ECS 采用**显式、受控的 component registration model**：

> 组件类型在 ECS 开始使用前必须完成 registration；registration 由明确的启动/初始化流程执行，而不是依赖隐式自动注册。

`RegisterComponentType<T>()` 继续作为 registration API，但其调用责任必须由明确的 ECS initialization boundary 承担。

### Decision Rationale

选择显式 registration，而不是隐式自动注册，原因是：

- 当前 `ComponentRegistry` 已存在独立的 registration 与 metadata 概念。
- `EntityManager` 的 archetype 构造依赖 `ComponentMeta`，registration 不是纯缓存，而是运行所需的元数据前置条件。
- 隐式 registration 会把"第一次使用类型"的副作用隐藏在 `AddComponent<T>()` / archetype creation 中，使 ECS 的初始化状态难以推理。
- 显式 registration 更容易形成 deterministic startup contract，并且不会依赖 C++ static initialization order。
- `629ea47` 已经证明"未注册"必须先表现为可诊断失败，但并没有证明"未注册类型应该自动可用"。

因此，当前证据更支持**显式注册是前置条件**，而不是自动注册。

### Runtime Safety Contract

在组件未完成 registration 时：

- 不得自动注册；
- 不得进入零-meta archetype；
- 不得产生 SEH / UB；
- 必须产生可诊断的拒绝/失败结果。

`629ea47` 已完成该 safety layer；`b8d7243` 已解决随后暴露的 location bookkeeping defect。

### ECS / GameObject Relationship

本决策**不要求 ECS 与 GameObject 两套组件模型立即收敛**。

在没有额外产品架构决定之前：

- ECS registry 是 ECS archetype metadata 的权威来源；
- GameObject 的 component model 保持现状；
- 不因为 registration decision 自动启动两套模型迁移或统一。

后续若决定两套模型收敛，再单独定义 type identity、serialization、lifecycle 与 migration contract。

### Explicit Non-Decisions

本决策不规定：

- registration 的最终调用点属于 Application、EngineHost 还是其他 bootstrap layer；
- registration 是否最终由生成代码辅助；
- ECS / GameObject 是否最终统一；
- 当前已有 component call sites 的完整迁移顺序。

这些属于实现计划或后续架构决策。

### Compatibility Note

现有代码中关于"registration 会在 static initialization 中自动完成"的注释与实际 API 行为不一致，应在实施阶段一并修正文档/注释。

但该文档修正不改变本决策本身：

> **ECS 不自动注册；使用前必须显式完成 registration。**

---

## 1. 对既有记录的更正（重要）

此前的工作记录中曾写：

> `RegisterComponentType<T>()` 无 production/test caller

**该表述错误。** 它混淆了两个同名但不同的函数。仓库中实际存在**两个** `RegisterComponentType`：

| | ECS 注册表 | JsonSerializer 工厂表 |
|---|---|---|
声明 | `engine/include/Engine/Core/ECS/ComponentRegistry.h:21` | `engine/include/Engine/Core/Scene/Serializer.h:102` |
形态 | `RegisterComponentType<T>()`（无参） | `RegisterComponentType<T>(const std::string& typeName)` |
存储 | typeID → `ComponentMeta` | 类型名字符串 → 工厂函数 |
**调用点** | `engine/src/Core/ECS/PhysicsComponents.cpp:17-21`（**5 处生产调用**：RigidBody3D / PhysicsRuntime / BoxCollider3D / SphereCollider3D / CapsuleCollider3D） | `SpriteComponent.cpp:16`、`PhysicsComponent.cpp:8` |

即 **ECS 注册表是有生产调用方的**。B4 的成因不是"注册机制从未落地"，而是另一件事（见 §2）。

本文档以更正后的事实为准。

---

## 2. 已验证的失败链

### 2.1 ECS 没有属于自己的 `Component` 基类

`engine/include/Engine/Core/ECS/` 中：

- `ECS.fwd.h:80` — `struct ComponentType`
- `ECS.fwd.h:91` — `struct ComponentMeta`
- **不存在** ECS 侧的 `Component` 基类
- `ECSBridge.h:24` — `class Component;` 是**前置声明**，指向 GameObject 侧的组件模型（`Engine/Core/GameObject/Component.h`）

### 2.2 ECS 采用 typeID → ComponentMeta 注册表

`EntityManager` 创建 archetype 时（`EntityManager.cpp:93-101`）：

```cpp
auto* meta = GetComponentMetaByTypeID(i);
if (meta) {                    // 未注册的类型被**静默跳过**
    metas.push_back(*meta);
}
...
assert(!metas.empty() && "Cannot create Archetype with no components");
```

未注册的类型不会报错，只会被跳过，最终导致 `metas` 为空。

### 2.3 测试使用的类型未注册、且无基类

`tests/test_ecs/EntityManagerTest.cpp:18-24`：

```cpp
struct Position { float x = 0, y = 0, z = 0; };
struct Velocity { float vx = 0, vy = 0, vz = 0; };
```

两者都是**普通结构体**：不继承任何基类，也没有调用任何注册函数。随后用例直接 `em.AddComponent<Position>(e)`。

### 2.4 完整因果链

```
Position 无 ComponentMeta（未注册）
  → archetype 收集到 0 个 meta
    → Debug：assert(!metas.empty()) 触发 → abort，exit 3
    → Release：assert 被编译掉 → 构造出"零 meta 的 Archetype"
      → 后续按无效存储访问 → SEH 0xc0000005 / 错误值
```

实测：`test_ecs` 11 个用例中 Debug abort、Release 5 个失败（含 2 次 `SEH 0xc0000005`）。

### 2.5 一处文档与实现的不一致

`ComponentRegistry.h:7` 的注释写：

> 每个组件类型在使用前必须注册。注册会在程序初始化时（static init）自动完成。

但 `RegisterComponentType<T>()` 是一个**需要被手工调用**的函数，并不存在"使用前自动注册"的机制。**注释描述的意图与实现不一致。**

---

## 3. 症状的性质

需要区分两类可能性，二者目前**无法从代码判定**：

- **(a) 测试写错了** —— `Position`/`Velocity` 应当是已注册的 ECS 组件，测试遗漏了注册。
- **(b) 测试正确反映了设计意图** —— ECS 侧确实不要求组件继承基类、也不要求预先注册，`AddComponent<T>` 应当让任意类型可用。

若为 (b)，则 `EntityManager.cpp:93-101` 的"静默跳过 + assert"是一个**缺陷**：它把"未注册"变成了未定义行为（Release 下 SEH），而不是可诊断的失败。

**判定 (a) 还是 (b) 需要产品意图，本文档不代为判定。**

---

## 4. 可行选项（注册时机维度）

### 选项 A — 显式注册为既定契约

未注册类型的使用方（含测试）负责注册。

### 选项 B — `AddComponent<T>` 首次使用时自动注册

类型在首次使用时惰性进入注册表。

### 选项 C — ECS 引入自己的组件概念（静态自注册）

以 CRTP 基类在静态初始化期自动注册，调用方无需显式动作。

---

## 5. 每个选项的后果

### 选项 A 的后果

| 维度 | 后果 |
|---|---|
`test_ecs` | 测试需要改为注册组件（或改用已注册类型） |
运行期安全 | 未注册类型仍走 `assert`；**Release 下仍是 SEH**，除非额外补运行期校验 |
性能 | 注册表查找开销保持现状，无热路径代价 |
文档 | 需修正 `ComponentRegistry.h:7` 的"自动完成"表述 |
风险 | 忘记注册在 Release 下静默 UB，**当前症状不会被根治** |

### 选项 B 的后果

| 维度 | 后果 |
|---|---|
`test_ecs` | 可原样通过（无需改测试） |
运行期安全 | 需处理"自动注册发生在使用点"的失败路径，否则仍可能构造零 meta archetype |
线程安全 | 注册表写入变成潜在热路径；需要确认是否在多线程场景被调用 |
确定性 | 注册时机从启动期推迟到运行期，**可复现性下降** |
文档 | `ComponentRegistry.h:7` 的表述反而更接近实现 |
风险 | 把"注册"变成隐式副作用，跨翻译单元的初始化顺序需要额外论证 |

### 选项 C 的后果

| 维度 | 后果 |
|---|---|
`test_ecs` | 测试类型需改为继承新基类，**测试需要改写** |
模型统一 | ECS 获得自己的组件概念，与 §6 的模型收敛问题直接相关 |
初始化顺序 | 依赖静态初始化顺序，跨 TU 顺序需保证（与 `PhysicsComponents.cpp` 现有手工调用可共存或需替换） |
二进制布局 | 新基类可能影响布局；需确认 `Chunk`/`Archetype` 的存储假设 |
成本 | 三选项中实现成本最高 |

---

## 6. 一个正交且更大的问题：是否收敛为单一组件模型

注册时机（§4）之外，仓库目前**并存两套组件模型**：

- **GameObject 模型**：`Engine/Core/GameObject/Component.h`，有基类；`JsonSerializer` 通过 `RegisterComponentType<T>(name)` 注册，并使用 `obj.AddComponent<T>()` / `GetComponent<T>()`。
- **ECS 模型**：typeID + `ComponentMeta` 注册表，**无基类**；`EntityManager::AddComponent<T>` / `GetComponent<T>`。

`ECSBridge.h:24` 的 `class Component;` 前置声明表明两者之间**已存在桥接意图**，但桥接的具体形态尚未确立。

**这一项可能比 §4 的注册时机更根本**：如果两套模型最终收敛，§4 的选择空间会被大幅压缩。是否收敛、以及收敛方向，同样需要产品/架构意图。

---

## 7. 未决问题

1. `test_ecs` 的 `Position`/`Velocity` 是**测试遗漏注册**，还是**在表达"ECS 不要求基类与预注册"的设计意图**？（对应 §3 的 (a)/(b)）
2. `ComponentRegistry.h:7` 声称的 "static init 自动完成注册" 是**原始意图**还是**过时注释**？
3. 未注册类型在 **Release** 下导致 SEH，是否需要改为可诊断的失败（运行期校验）？这与选择哪个注册模型**独立**，可先行决定。
4. ECS 与 GameObject 两套组件模型是否收敛？若收敛，方向与时间表？
5. `AddComponent<T>` 是否允许零组件实体？（`assert(!metas.empty())` 隐含"不允许"，但该约束未见于文档）

---

## 8. 与其他待决事项的关联

| 关联项 | 关系 |
|---|---|
`docs/ADR-Audio-Canonical-Stack.md` §4 未决问题 5 | 音频 ADR 亦记录"是否存在 ECS 迁移计划"，两者指向同一架构意图 |
`JsonSerializer` 组件工厂表 | 已覆盖测试（`a0f7d99`），其注册模型**不受本文档结论影响** |

---

## 9. 本文档明确不做的事

- §1–§10 的分析不推荐任何选项（§0.5 的草案由决策方提交，非本文档推荐）
- 不判定 `test_ecs` 是"测试缺陷"还是"实现缺陷"
- 不把"需要显式注册"或"需要自动注册"写成技术事实 —— §0.5 的草案是决策而非技术事实
- 不宣称 §6 的模型收敛应当发生
- 不因 §0.5 的草案而修改任何代码

---

## 10. 现状处置

**已关闭**（原 §2.2 记录的未注册类型失败链）：

- `629ea47` —— 未注册类型不再经 Release SEH 进入未定义失败，改为确定且可诊断的拒绝（`AddComponentRaw` 入口守卫 + `AddComponent<T>` 不再解引用 nullptr）。守卫**不自动注册**。
- `b8d7243` —— 随后暴露的 `Chunk::RemoveRow` swap-with-back 位置簿记缺陷已修复，覆盖 `DestroyEntity` / `MigrateEntity` / `RemoveComponentRaw` 三条路径。

`test_ecs` 现为 **18/18**，三配置全量 suite 均 **16/16**，无失败项。

**仍 OPEN**：注册模型本身（显式 / 隐式 / 运行时）——§0.5 已提出草案但**尚未接受**；以及 §6 的 ECS / GameObject 模型是否收敛。两者均需架构决策，不阻塞其它验证工作。