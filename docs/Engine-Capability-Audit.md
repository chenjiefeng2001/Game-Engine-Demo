# Engine Capability Audit — 与主流引擎对比及演进规划

**审计基准**：`HEAD d3ba590`（branch `avalonia`）
**审计日期**：2026-10-02
**证据来源**：HEAD tracked 文件清单、测试发现（257 tests / Debug 全绿）、子系统 LOC、
stub/placeholder 扫描、`IRHI*` 接口形态、CI matrix。
**状态**：审计结论已冻结；本文件**不含**任何代码修改。

> ⚠ **证据边界**：本审计**不**包含运行时 profiling、内存 profiling、跨平台实机执行
> （Linux 构建当前失败，见 §7）、资源覆盖率测量，或任何 gameplay 质量判断。
>
> ⚠ **HEAD 范围**：工作树有 126 项未提交条目，**领先于** HEAD。本报告为 **HEAD-scoped**，
> 部分实际工作可能比此处评估的更新。
>
> ⚠ **"成熟度"为推断**：由代码体量 + 测试存在性推断，**非**实测。

---

## 1. 引擎定位

自研 **C++20 引擎**，含 ImGui 内嵌编辑器与 Avalonia/.NET bridge。
渲染、物理、ECS、动画、音频、脚本**全部自研**；第三方仅 GLFW / GLM / Jolt / Box2D /
OpenAL / Lua / spdlog / ImGui / Vulkan SDK / Tracy / shaderc / SPIRV-Cross。

规模：**331 engine headers**，engine ~100k 行 · bridge ~3k · tests ~9k · docs ~12k。

---

## 2. 子系统成熟度矩阵

图例：**●** 生产级 · **◐** 部分/受限 · **○** 已声明但单薄 · **✗** 缺失

| 子系统 | 实现 | 测试 | 成熟度 | 备注 |
|---|---|---|---|---|
| Core/ECS | 8 文件 | 13 | ● | 组件生命周期、实体管理 |
| Core/Job/Threading | 6 文件 | 4 + stress | ● | TaskGraph、TaskManager、线程亲和 |
| Core/Memory | 分配器 | 2 | ● | MemoryTracker、面板 |
| Core/IO | FileSystem / FileStream / PakFile / AsyncStream | **0** | ◐ | **零直接测试** |
| Core/Config | Config / ConsoleVariable / UserSettings | **0** | ◐ | 无测试 |
| Platform | GlfwWindow / GlfwInput / FileDialog | **0** | ◐ | |
| Rendering/OpenGL | 25+ 文件 | 9 | ● | GL46 AZDO device、sprite batch、GBuffer |
| Rendering/RHI | 13 文件（`IRHI*`） | （经 renderer） | ● | 抽象干净，4 后端 |
| Rendering/Vulkan | 20 文件 | **0** | ◐ | **loader + WSI 绑定 Win32**（§7） |
| Rendering/D3D12 | 1 文件 | **0** | ◐ | 按设计 dormant（有文档） |
| RenderGraph + passes | RenderGraph / CSM / Deferred / IDBuffer / ComputeCulling / PostProcess | 9 (SG6) | ● | 相当成熟 |
| GPU Particles | GPUParticleSystem / TransientHeap / BindlessTexture | 54 (physics) | ● | compute 驱动 |
| Editor | 42 文件 | 22 (GP01) | ● | 完整 ImGui 编辑器、undo、asset browser、依赖图 |
| Physics (Jolt 3D) | 6 文件 | 54 | ● | |
| Physics (Box2D 2D) | 8 文件 | 含于上 | ● | 双后端 |
| Scripting (Lua) | 5 文件 | 27 | ● | 插件系统、C ABI |
| Animation | 29 headers | **0** | ◐ | **完整 skeleton/IK/blend/retarget，无测试** |
| Audio | 13 文件 | **0** | ◐ | OpenAL、bus、空间化 —— **无测试** |
| UI/UIHelpers | UIManager / HUD | **0** | ◐ | |
| Debug/Tools | CrashHandler / Profiler / Screenshot | **0** | ◐ | 已接 Tracy |
| NavMesh/AI | — | — | ✗ | **缺失** |
| Networking | — | — | ✗ | **缺失** |
| Shader authoring | ShaderGraph / VFXGraph | **0** | ◐ | 已声明 |
| Asset cook/stream | DXGoldenGate / ResourceLifecycle | 42 (content) | ◐ | |
| Localization | — | — | ✗ | 缺失 |

**代码健康**：89 个空函数体（虚函数默认实现 / accessor，属正常）；标记
15 TODO / 13 stub / 7 placeholder / 18 占位 —— 集中在 editor 与 VFX 工具链。
候选集中 **0 secrets、0 文件 >5MB**；17 个 submodule 均锁定精确 SHA。

---

## 3. 与主流引擎对比

| 能力 | **本引擎** | **Unreal 5** | **Unity** | **Godot 4** | **Bevy** |
|---|---|---|---|---|---|
成品（AA 级出货） | vertical-slice | 完整 | 完整 | 完整 | ECS-first |
渲染 | GL46 AZDO + RenderGraph + CSM/Deferred | 全 RD | SRP | Forward+/Mobile | wgpu |
RenderGraph | ● 自研 | ● | ✗（SRP） | ✗ | ✗ |
物理 | Jolt **且** Box2D | Chaos/PhysX | PhysX | Godot Physics | Rapier |
脚本 | Lua + C ABI | Blueprints/C++/Python | C# | GDScript/C# | Rust |
ECS | 自研 | UE5 原生 | DOTS（可选） | ✓ 原生 | ✓ 核心 |
编辑器 | ImGui，进程内 | 完整 IDE | 完整 IDE | 完整 IDE | 无（bevy_inspector）|
编辑器完成度 | ~Unreal 的 20% | — | — | — | — |
NavMesh | ✗ | ● | ● | ● | ✗（第三方）|
Networking | ✗ | ● | ● | ●（多人）| ✗ |
Asset cook/stream | ◐ | ● 成熟 | ● | ● | ◐ |
Shader 创作 | ShaderGraph/VFXGraph（◐） | 完整材质编辑器 | ShaderGraph | 可视化编辑器 | ✗ |
热重载 | ◐ | ● | ● | ● | ◗ |
Console/REPL | ●（CVar + 命令）| ● | ~ | ● | ◐ |
Undo/redo | ● | ● | ~ | ● | ✗ |
崩溃上报 | ●（ASan 感知）| ● | ~ | ● | ~ |
GPU 粒子 | ● compute | ● | ● | ● | ◐ |
Bindless | ● | ● | ✗ | ✗ | ✗ |

**诚实定位**：这是一个**可信的中端引擎基础**。渲染、编辑器、物理、ECS、job system
四层具备真实生产形态，某些方面比典型业余引擎更精细。但与 Unreal/Unity 在**出货内容、
工具广度、平台矩阵、资源管线**上不可比。

与"可出货引擎"的真实差距**不在代码量**，而在：
**navmesh、networking、动画/音频测试覆盖、资源 cook 健壮性、平台可达性**。

---

## 4. 实现矩阵（验证状态）

| 子系统 | 已实现 | **已测试验证** | 未测试面 |
|---|---|---|---|
ECS | ● | 13 | — |
Physics | ● | 54 | GPU/CPU 一致性 |
Scripting | ● | 27 | — |
Content pipeline | ◐ | 42 | 负载下 streaming |
Editor workflow | ● | 22 | — |
Rendering/RHI | ● | 9 | D3D12/Vulkan 路径 |
Job system | ● | 4+stress | — |
Pick (D3) | ● | 8+6 | — |
Bridge/ABI | ● | 44+50 | — |
**Animation** | ● | **0** | 29 headers —— skeleton / IK / blend tree / retarget / compression |
**Audio** | ● | **0** | 13 文件 —— bus / 空间化 / effect |
**IO/FileSystem/Pak** | ◐ | **0** | streaming、Pak |
**Config/CVar** | ◐ | **0** | 解析、持久化 |
**Vulkan 后端** | ◐ | **0** | 且不可移植 |
**D3D12** | ◐ | **0** | dormant |
**NavMesh** | ✗ | — | 缺失 |
**Networking** | ✗ | — | 缺失 |

> **关键洞察**：**风险加权的缺口是 Animation 与 Audio** —— 二者体量大、声称完整、
> 却**没有任何测试**。这比一个未测试的小工具更糟，因为**未测试的复杂代码只会静默失败**。

I2 baseline **251**；当前 discoverable **257**（差值 6 = D3 transport tests，
按既定流程**刻意未** baselined）。

---

## 5. 阻塞问题（承接既有审计）

| ID | 问题 | 严重度 |
|---|---|---|
**S1** | `ScriptInstance` 嵌套 NSDMI 默认参数 —— 已修复并验证，**未进历史** | 高 |
**F1** | `Application.cpp` 大小写冲突；deletion 已验证，但**在 Windows 上不可 land** | 高 |
**S2** | Vulkan loader/WSI 绑定 Win32；intent 未决（Outcome C）| 高 |
**F12** | CI 缺 `libvulkan-dev`；Linux lane 实质无监控 | 中 |
**NEW-1** | pristine HEAD 在 Linux **构建失败**（`glfw3native.h`→`windows.h`、`ScriptInstance`）| **对任何跨平台声明为 critical** |

---

## 6. 演进规划（按差距点）

### P0 — 让 Linux CI 说真话

**差距**：CI 声称提供可移植性证据，实际基线无法构建，lane 长期红/被忽略。
**目标**：Linux 构建绿，且失败必须真实可见。
**方案**：修复 `ScriptInstance`（S1，已验证）→ 修 `RHIWindow.cpp` 的 `glfw3native.h`
（**先做 S2 audit 结论落地，不可反射性加 `_WIN32`**）→ 补 `libvulkan-dev`（F12）。
**验收**：pinned `d3ba590` 在 `ubuntu-latest` + g++/clang++ 全配置构建通过。
**风险**：S2 的正确修法依赖 backend 策略决策；在该决策前，Linux lane 只能标为已知红。
**规模**：M（配置 + 少量源改动），但**被 S2 决策阻塞**。

### P0 — 打通 F1/S1 的 landing 路径

**差距**：大小写冲突使干净的历史形成成为不可能；Windows 侧无法 cherry-pick/rebase。
**目标**：删除可 land，且不留下同类复发陷阱。
**方案**（三选一，需显式 scope 决策）：
- **F1-only**：在 case-sensitive 环境 land，**并在 commit message 与本文件中永久记录**
  Windows 不可 apply 的约束；
- **Repository hygiene 迁移**：消除冲突本身，使 Windows checkout 行为正确 —— 范围最大，
  需独立 review 与回滚方案；
- **Hold**：artifact 已 hash 验证，保持不变。
**验收**：`git cherry-pick` 在干净 Windows checkout 上成功；S1 随之进入历史。
**风险**：F1-only 留下复发风险；hygiene 迁移影响所有人的 checkout。
**规模**：F1-only = S；hygiene 迁移 = L。

### P1 — Animation / Audio 测试补齐

**差距**：29 个 Animation header + 13 个 Audio 文件，**零测试**，且声称完整。
**目标**：把"声称完整"转为"已验证"。
**方案**：Animation 优先（风险更高）—— skeleton 层级遍历 → IK solver 收敛 →
blend tree 权重归一 → retarget 骨架映射 → compression 往返保真。
Audio 次之 —— bus 图路由 → 空间化衰减/声像 → effect chain 顺序。
**验收**：每个已声明的公开能力至少一条 behaviour 测试；不得以 header 存在作为完成依据。
**风险**：动画数学易写出"看起来对"的弱测试，须以**不变量**（骨骼数守恒、权重和=1、
末端 effector 误差阈值）断言。
**规模**：M–L（Animation 约 60–90 测试，Audio 约 30–50）。

### P1 — IO / Config 测试补齐

**差距**：FileSystem / PakFile / AsyncStream / Config / CVar 零测试，但属**基础设施**，
失败会静默污染上层。
**方案**：Pak 索引与偏移、异步读取顺序与竞态、CVar 注册/优先级/持久化往返。
**规模**：S–M。

### P2 — Vulkan backend 策略决策（产品决策）

**差距**：文档称 Vulkan-first 跨平台，HEAD 实现 loader/WSI 全为 Win32；
`CreateVulkanSurface()` 存在但**零调用者**。Intent unresolved（Outcome C）。
**目标**：由**产品/架构 owner** 决定方向，代码无法回答。
三选一：**(a)** 保持 Windows-only 并门控 `ENGINE_HAS_VULKAN` + 修正文档；
**(b)** 通过既有 `CreateVulkanSurface()` 抽象恢复可移植 WSI；
**(c)** 暂不动，仅文档化限制。
**验收**：决策被记录；对应 DoD 明确；Linux Vulkan 要么真能跑，要么明确不可用。
**规模**：(a) S · (b) L · (c) S。

### P2 — Asset cook / streaming 健壮性

**差距**：42 个 content 测试集中在 pipeline，streaming 在负载下无覆盖。
**方案**：大资源 streaming 内存上界、缺失依赖的确定性失败、cook 缓存失效矩阵。
**规模**：M。

### P3 — NavMesh / Networking（若目标是"可出货引擎"）

**差距**：两者**完全缺失**。这是与主流引擎最大的功能性缺口。
**方案**：NavMesh 先（Recast/Detour 或自研），Networking 后（传输抽象 + 复制/兴趣管理）。
**前置**：先完成 P0/P1，否则新增子系统会继承当前的验证债。
**规模**：NavMesh = L，Networking = XL（均需独立 charter）。

---

## 7. 建议执行顺序

1. **F1/S1 landing 路径**（解除历史阻塞）—— 需 scope 决策。
2. **S2/Vulkan 策略决策**（解除 P0 的 Linux 阻塞）。
3. **P0 Linux CI 真实化**（依赖 1 与 2）。
4. **P1 Animation → Audio → IO/Config 测试**（最高风险加权收益）。
5. **P2 asset streaming**。
6. **P3 NavMesh / Networking**（仅当目标是可出货引擎）。

顺序理由：1 与 2 都是**决策 + 解除阻塞**，先做完才能让 3 真正可执行；
而 4 无论 1–3 如何都应尽早开始，且不依赖任何外部决策。

---

## 8. 审计限制（明确声明）

- 仅静态 + 测试发现证据；无运行时 profiling、无 gameplay 评估。
- "成熟度"由代码量与测试存在性推断，非实测。
- 工作树 126 项未提交，**领先于 HEAD**；本报告 HEAD-scoped，可能低估实际进度。
- Animation / Audio 的"已实现"判定**仅基于 header 清单**，属弱推断 —— 已在 §4 标注，
  这正是把它们列为首要风险的原因。
