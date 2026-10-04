# 待决事项说明（Decision Brief）

> **状态**: 待决策
> **最后更新**: 2026-10-04
> **用途**: 把当前所有**需要人来决定**的事项集中成一份可决策清单
> **不重复** ADR 内容 —— 事实、选项、后果详见 `docs/ADR-Audio-Canonical-Stack.md` 与 `docs/ADR-B4-ECS-Registration.md`
> **本文档不含架构结论**。技术事实已穷尽，剩余判断依赖产品/架构意图

---

## 0. 全局状态

**已关闭**（无需任何决策）：

| 项 | 证据 |
|---|---|
B5-RD 配置缺陷 | `6302c9d` |
B5-RD 可复现性 | `510fefb` |
P1 Animation / Audio / IO | `96b6c92` / `ea195d1` / `a59c725` |
Serializer sweep | `a0f7d99`、`cf1570c`、`4c3133a`、`48d2d52`、`1503804` |
Stage 2 headless 盘点 | `eb73f08` |
Time Stage 3 | `4277f28`、`9033610` |

**当前工程基线**：Debug / Release / RelWithDebInfo 三配置全量 build 0 error；全量 suite 均 15/16，唯一失败为已知 `test_ecs`。

---

## 1. 需要决策的两项

### 决策一：Audio canonical stack

**要决定的问题**：音频以哪一套 OpenAL 实现为 canonical —— Stack 2（`IAudioEngine` 抽象）、Stack 1（`Engine::Audio/AudioSource` 直接实现）、或正式承认两者共存。

**为什么必须由人决定**：技术审计已穷尽（详见 ADR）。三个选项在**所有权、生命周期、有效性语义、公开 API** 上的差异无法用技术证据消解 —— 每种选择都对应一个不同的产品承诺。

**为什么现在不能拖**：两套栈并存的现状正在持续产生实际成本，其中一项是**文档层面引导新代码走绕过路径**（`AudioClip.h:22` 直接示范 `alSourcei(source, AL_BUFFER, clip->GetBufferHandle())`）。

**选项与后果**：见 ADR §2、§3。

**决策后立即发生什么**：

| 选项 | 后续工作性质 |
|---|---|
Stack 2 canonical | 需要 production API 变更（`AudioClip` 产出 `IAudioBuffer` 的路径 + `AudioSourceComponent` 注入 `IAudioEngine&`），并迁移三处 native-handle 消费点 |
Stack 1 canonical | `IAudioEngine` / `IAudioSource` 需重新定位；已交付的 112 个 `test_audio` 测试需重新归属 |
共存（形式化现状） | 无代码工作，但 `m_BufferID` 从"过渡实现"变为**永久公开契约**，且 `AudioSourceComponent::Play` 中对接口的 no-op 调用将长期保留 |

**必须避免的中间态**：半迁移会同时存在两条 buffer 来源路径 —— 比现状更难推理。

**建议参与方**：音频系统负责人 + 引擎架构 + 使用方（editor / bridge / sandbox）。

---

### 决策二：B4 ECS 组件注册模型

**要决定的问题**：ECS 侧组件是否需要预注册、以何种方式注册；以及 ECS 与 GameObject 两套组件模型是否收敛。

**关键歧义（尚未判定）**：`test_ecs` 的失败究竟属于

- (a) **测试遗漏注册** —— `Position` / `Velocity` 本应是已注册组件
- (b) **测试正确表达了设计意图** —— ECS 确实不要求基类与预注册，`AddComponent<T>` 应让任意类型可用

**若为 (b)，则 `EntityManager.cpp:93-101` 本身是缺陷**：它把"未注册"变成未定义行为（Release 下 SEH），而非可诊断失败。**此判定无法从代码得出。**

**更正记录**：`engine/include/Engine/Core/ECS/ComponentRegistry.h:7` 注释声称"注册会在程序初始化时（static init）自动完成"，但 `RegisterComponentType<T>()` 是需手工调用的函数 —— **文档意图与实现不一致**，需一并澄清。

**选项与后果**：见 ADR §4、§5。

**建议参与方**：ECS 负责人 + 引擎架构。

---

## 2. 两项决策的耦合（建议合并评审）

ADR 之间已交叉引用，耦合点如下：

```
B4 §6：ECS(typeID + ComponentMeta，无基类) 与 GameObject(Component 基类 +
       JsonSerializer 工厂表) 两套模型是否收敛？
                    ↕
Audio §4 未决问题 5：是否存在把音频从 GameObject/Component 模型迁移到
                    ECS 模型的计划？
```

**如果两套组件模型最终收敛，决策二的选项空间会被大幅压缩**，而音频的 canonical stack 选择也会被牵动。因此**建议在同一次架构评审中处理两项**，避免先决定音频、随后因 ECS 模型变动而返工。

---

## 3. 可以脱离上述决策、先行决定的一项

**未注册组件类型在 Release 下导致 SEH，是否应改为可诊断的失败（增加运行期校验）？**

此项与"选哪种注册模型"**完全独立**（ADR §7 未决问题 3），可立即单独决定：

- 若决定增加运行期校验 → 在 `EntityManager` 的 archetype 创建路径上加运行期检查，使 Release 下的未注册类型产生明确失败而非 SEH。这会**独立于**注册模型的最终选择生效，且立即提升 `test_ecs` 的可诊断性。

---

## 4. 不需要决策、仅作观察的两项

这两项**当前证据均为"观察 1 次，后续多次干净"**，不满足升级为 defect 的门槛，**不建议投入进一步调查**（投入不会增加确定性）。

| 项 | 记录 | 重新评估的触发条件 |
|---|---|---|
`test_renderer` timeout | 1 次超时；已加 `TIMEOUT 180` 约束（`38857c6`）；后续多次干净 | 再次复现，或出现可定位的耗时分布证据 |
`test_pick_transport` timeout | 1 次超时；后续 4 次干净 | 再次复现 |

**已知但已定性的相邻事实**：`test_pick_transport` 依赖 `assets/gp01/manifest.json` 的**相对路径**，因此工作目录相关；ctest 已正确设置 `WORKING_DIRECTORY`，故非缺陷。**不建议**为此引入 CWD 操纵。

---

## 5. 隔离中、不得混入上述决策的工作区内容

| 内容 | 状态 |
|---|---|
`tests/CMakeLists.txt` 4 个 unstaged hunk | HRC-3，全程隔离 |
`engine/src/Core/Time.cpp` `+10/−0` | HRC-3（`Time::Shutdown` 实现） |
`engine/src/Core/Application.cpp` `+338/−12` | HRC-3（新增 `Application::Shutdown` 等） |
`bridge/src/EngineHost.cpp` | untracked，HRC-3 |

**归属提醒**：`Time::Shutdown()` 与 `Time::IsInitialized()` 两个 API 目前**只服务于 HRC-3 的未提交工作**（唯一消费者分别位于上述 `Application.cpp` 与 `EngineHost.cpp`）。`9033610` 中覆盖这两个 API 的用例，若 HRC 后续改变其接口，是**唯一**需要迁移的部分。

---

## 6. 恢复工作的条件

| 想做的事 | 前置条件 |
|---|---|
推进 Time 之外的 Stage 3 | 无条件可做，但阶段 2 已判定无其它值得进入的 Category 1 面 |
修复 `test_ecs` 使 suite 达到 16/16 | 需先判定 §1 决策二的 (a)/(b) 歧义；或先决定 §3 的运行期校验 |
清理音频绕过路径 | 需 §1 决策一 |
引入 RelWithDebInfo/macOS/Linux-ASan CI 覆盖 | 需独立决策（当前 CI 已覆盖 Windows RelWithDebInfo+ASan，刻意未扩 macOS 与 Linux ASan） |

---

## 7. 本文档不做的事

- 不推荐任何架构选项
- 不判定 `test_ecs` 属于测试缺陷还是实现缺陷
- 不把"应显式注册"或"应自动注册"写成技术事实
- 不把观察项升级为 defect
- 不修改任何代码