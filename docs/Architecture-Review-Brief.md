# 架构评审说明（Audio canonical stack + B4 ECS registration）

> **状态**: 待评审（Review Pending）
> **最后更新**: 2026-10-04
> **评审对象**: `4b1f21f` 中记录的两份 **Status: Proposed** 草案
> **用途**: 为架构评审提供可核查的决策依据；**本报告不投票、不推荐、不预判结论**

---

## 0. 评审对象与当前状态

| 草案 | 位置 | 状态 | 是否已触发实现 |
|---|---|---|---|
Audio canonical stack = Stack 2 | `docs/ADR-Audio-Canonical-Stack.md` §0.5 | **Proposed（待接受）** | 否 |
B4 ECS 显式受控注册 | `docs/ADR-B4-ECS-Registration.md` §0.5 | **Proposed（待接受）** | 否 |

工程基线：Debug / Release / RelWithDebInfo 三配置全量 build `0 error`，全量 suite `16/16`，`test_ecs` `18/18`。HRC-3 工作区全程隔离。

---

## 1. 证据基础与质量分级

评审时区分两类论据，二者强度不同：

### 1.1 已验证事实（附源码位置，可复核）

| 事实 | 位置 |
|---|---|
`IAudioSource::Play` 接收 `std::shared_ptr<IAudioBuffer>` | `IAudioSource.h:26` |
`IAudioEngine::CreateBuffer` 返回 `shared_ptr<IAudioBuffer>`；`CreateSource()` | `IAudioEngine.h:38`、`:46` |
`AudioClip::m_BufferID` 存在、对外暴露为 `GetBufferHandle()`、并参与 `IsValid()` | `AudioClip.h:135`、`:80`、`:98-99` |
`AudioSourceComponent` 绕过接口：`alSourcei` 绑定后调用 `m_Source->Play(nullptr)` | `AudioSourceComponent.cpp` |
未注册类型曾导致 Release SEH；现已收敛为确定失败 | `629ea47` |
swap-with-back 位置簿记缺陷已修复，覆盖三条调用路径 | `b8d7243` |

### 1.2 论证性判断（草案的 Rationale，属观点而非事实）

- "Stack 2 的抽象已表达正确的生命周期边界"
- "显式 registration 更易形成 deterministic startup contract"
- 两者均为**架构判断**，评审可接受、修改或否决；不应被当作既有技术事实引用。

---

## 2. Audio：迁移范围量化

草案列出未来迁移必须一次性解决的三个问题。以下为当前代码的实测范围，供评估工作量。

### 2.1 原生 OpenAL 调用分布（共 40 处，9 个文件）

**属于 Stack 2 后端实现（迁移后应保留，13 处 / 3 文件）**

| 文件 | 处数 |
|---|---|
`OpenALAudioSource.cpp` | 9 |
`OpenALAudioBuffer.cpp` | 3 |
`OpenALAudioSource.h` | 1 |

**属于绕过接口或 Stack 1（迁移目标，27 处 / 6 文件）**

| 文件 | 处数 | 性质 |
|---|---|---|
`AudioSource.cpp` | 10 | Stack 1 直接 OpenAL（自有 `ALuint` / context / source 生命周期） |
`AudioClip.cpp` | 5 | self-upload 原生 buffer（`alDeleteBuffers` 等） |
`AudioSystem.cpp` | 5 | Stack 2 系统绕过接口：`alSourcei` + `alSourcePlay` |
`AudioSourceComponent.cpp` | 3 | 绕过绑定后 `Play(nullptr)` 为 no-op |
`AudioEngine.cpp` | 2 | 仅 `alGetError` 设备查询 |
`AudioClip.h` | 2 | **头文件注释中示范的绕过写法** |

### 2.2 需评审注意的即时矛盾

`AudioClip.h:79-80` 的注释直接示范 native handle 用法。该注释与草案 **Interim Rule**（"不新增绕过 `IAudioSource` 的生产路径"）方向相反，且它会**引导新代码复制绕过写法**。这是接受草案后应优先处理的一项**文档修正**，成本极低。

### 2.3 草案留给评审的问题

1. `AudioClip` 的 native buffer 最终保留还是移除？
2. `AudioClip` 是否永久保留 PCM？
3. OpenAL backend 是否需要 null-driver / headless policy？
4. Stack 1 的删除时间点？
5. 现有 112 个 headless audio tests 的最终归属？
6. 上述三问（`IAudioBuffer` 产出、engine context 获取、bypass 收敛）如何在**同一次**迁移中解决，避免中间态出现两条 buffer provenance path？

---

## 3. B4：迁移范围量化

### 3.1 当前生产注册方式与草案冲突

现有**全部 5 处**生产注册集中于 `PhysicsComponents.cpp:17-21`，并由函数内 static 初始化 lambda 触发：

```cpp
static bool s_Registered = []() {
    RegisterComponentType<RigidBody3DComponent>();
    RegisterComponentType<PhysicsRuntimeComponent>();
    RegisterComponentType<BoxCollider3DComponent>();
    RegisterComponentType<SphereCollider3DComponent>();
    RegisterComponentType<CapsuleCollider3DComponent>();
    return true;
}();
```

这正是草案**明确排除**的形态：隐式自动注册，且依赖 C++ static initialization order —— 而草案 Rationale 的第 4 条正是以"不依赖 static initialization order"为理由。**接受草案即意味着必须改写这一现有模式**，草案的 Explicit Non-Decisions 未提及此事。

### 3.2 未注册类型的实际缺口（1 处）

全仓 ECS 组件调用共 53 处 / 5 个类型，逐调用点核对接收者后：

| 类型 | ECS 调用点 | 注册状态 |
|---|---|---|
`Position`（测试类型） | 32 | 由 `SetUpTestSuite` 注册 |
`Velocity`（测试类型） | 7 | 由 `SetUpTestSuite` 注册 |
`UnregisteredComponent`（测试专用） | 10 | 故意不注册 |
`PhysicsRuntimeComponent` | 3 | 已注册 |
**`Joint3DComponent`** | **1**（`PhysicsSyncSystem.cpp:59`） | **未注册** |

即：**唯一真实缺口是 `Joint3DComponent` 一处**。B4 的注册面补齐因此是有界且可枚举的工作。

### 3.3 易被误计为缺口的项（评审需排除）

以下类型的调用接收者是 `obj->`，属 **GameObject 组件模型**，不在 ECS 注册范围：

`SpriteComponent`(6)、`MeshComponent`(11)、`MeshRendererComponent`(4)、`PhysicsComponent`(6)

另有若干**仅存在于文档注释**的示例，不构成运行时违规，但同样示范了"未注册即使用"：`EntityCommandBuffer.h:17`、`SkinningComponent.h:31`、`PhysicsComponent3D.h:20,24`。

### 3.4 与草案冲突的文档注释（Compatibility Note 的实际范围）

草案提到 `ComponentRegistry.h:7` 一处注释与实现不符。实测**共 3 个文件、5 处**：

- `ComponentRegistry.h:7`
- `PhysicsComponents.cpp:3` 与 `:5`（文件级注释亦称"在 static init 阶段自动注册"）

### 3.5 草案留给评审的问题

1. registration 的调用点归属：`Application`、`EngineHost`，还是独立 bootstrap 层？
2. 是否用生成代码辅助注册？
3. ECS 与 GameObject 是否最终统一？
4. 既有 component call site 的迁移顺序？
5. 是否接受改写 `PhysicsComponents.cpp` 的 static-init 模式（见 3.1）？

---

## 4. 两项草案的耦合与排序约束

### 4.1 已确认一致，无冲突

B4 明确**不要求** ECS 与 GameObject 收敛；Audio 草案方向**未回答**其 §4 未决问题 5（是否存在音频 ECS 迁移计划）。两者互相不阻塞，可独立评审、独立接受。

### 4.2 需要评审注意的排序约束

B4 要求把 registration 责任落到"明确的 ECS initialization boundary"，但**明确推迟**了具体归属。而 HRC-3 正在并发新增 `Application::Shutdown`（`engine/src/Core/Application.cpp` `+338/−12`）与 `bridge/src/EngineHost.cpp`（untracked）—— 恰是最自然的 bootstrap 层候选。

因此评审需要明确：B4 的 bootstrap 边界决定**是否允许依赖尚未提交的 HRC-3 工作**。建议评审显式表态，避免日后以"等 HRC 合并"为由无限推迟 B4 实施。

### 4.3 明确不合并的原则

两项草案分属不同子系统与不同生命周期。若同时被接受，建议**分别立项、分别设定迁移边界**，不合并为一次大重构。

---

## 5. 评审需要产出的结论

| # | 待产出 | 关联 |
|---|---|---|
1 | 是否接受 Audio = Stack 2（接受 / 修改 / 否决） | §2.3 |
2 | 是否接受 B4 显式注册（接受 / 修改 / 否决） | §3.5 |
3 | B4 的 bootstrap 边界归属，且是否允许依赖 HRC-3 | §4.2 |
4 | 是否接受"两项分别立项、不合并迁移" | §4.3 |
5 | 接受后是否为每项单独授权 migration plan | §2.3、§3.5 |

**在 1、2 得到答复前，两项保持 Proposed，不启动任何迁移实现。**

---

## 6. 本报告不做的事

- 不投票、不推荐、不预判评审结论
- 不把 §1.2 的论证性判断写成技术事实
- 不实施任何迁移、不修改任何代码
- 不触碰 HRC-3 工作区
- 不把 `Joint3DComponent` 一处缺口夸大为系统性缺陷
- 不重新打开已关闭项（B4 §3、row bookkeeping、Time Stage 3）