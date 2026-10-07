# 待决事项说明（Decision Brief）

> **状态**: 评审已完成 —— 两项决策均**已接受**（2026-10-04）；B4 已实施，A0 已实现待整合，A1 冻结；**新增 §8 CI dependency/build-boundary = OPEN**
> **最后更新**: 2026-10-06
> **用途**: 记录决策**做出前**需要回答的问题、选项空间与决策依据。结论见各 ADR §0.5（Accepted），本文档的选项分析仅作决策留痕
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
B4 §3 runtime-safety follow-up | `629ea47`（未注册类型不再经 Release SEH 进入未定义失败） |
ECS internal location bookkeeping defect | `b8d7243`（swap-with-back 后同步 `m_Locations`） |
B4 registration migration | `89d58dc`、`734a9bd`（`InitializeComponentRegistry` + `Joint3DComponent`；`test_ecs` 21/21） |
`Core`/`core` `Application.cpp` case collision | `1b83d95`（F1 CLOSED：case-sensitive checkout 干净 + canonical source build 通过） |
committed TimeTest 的 HEAD 自洽性 | `a6e788c`（移出 7 个依赖未提交 HRC API 的用例；纯 HEAD 首次可独立构建） |
`OpenALAudioBuffer` destructor context guard | `8f15827` |

**Committed test baseline：`16c029b`**（Phase 5.0 实测，clean worktree，零 HRC-3 污染）

| 口径 | 结果 |
|---|---|
| **committed test targets** | **13/13 build + 13/13 suite pass** |
| 配置覆盖 | **Debug / Release / RelWithDebInfo + ASan**（三配置） |
| gtest case | **699** 全部实跑通过 |
| HRC-3 的 3 个 targets | **不计入** committed baseline |

> **措辞说明**：本基线**不是**"三配置 full build `0 error`"。已验证的是 13 个 committed test targets 全部成功构建并运行；`sandbox/SpirVTest` 存在既有 linker mismatch（`6302c9d` / `510fefb` 已有专门记录），该已知限制与本表述相容，不在此重复。

**Fresh baseline verification 需要的仓库既有外部前置**（本次已 provision，因此本结果应读作 *"committed source + required fixture set"*，**不是**裸 fresh checkout）：

- **12 个 gitignored fixture**：`assets/gp01/Main.scene` + `dogfood0{1,2,3,5,6,7}.{scene,manifest.json}`（`.gitignore:88-89` 排除）
- **本机 Vulkan SDK** —— §8 已记录的未声明机器本地依赖
- **`510fefb` 的可复现 shaderc 预编译产物**（Debug / Release / RelWithDebInfo-ASan 三配置）

**更正记录（2026-10-06 / 2026-10-07 两次）**：
1. 本表此前将纯 HEAD 记为"三配置 `0 error` / 13/13"。实际仅 Debug 与 Release 经独立验证；**RelWithDebInfo 13/13 属外推，无直接证据**。该行旧表述已作废。
2. 上表整体已被 `a6e788c` 时期的记录取代 —— 其后有 **2 个 commit 改动 engine 源码**（`f32d113` 崩溃报告 JSON、`f9cf7db` JobSystem 自死锁），其中 `f32d113` 此前**从未被任何 suite 覆盖**（Debug/Debug 子系统无测试 target）。Phase 5.0 已为其补齐三配置全量构建 + 全量 suite 证据。

**已知验证缺口**（非 defect，独立 OPEN）：`.gitignore:88-89` 排除 `*.scene` / `*.manifest.json`，fresh checkout 缺少被忽略的本地夹具时 suite 为 **10/13**，补齐后恢复 13/13。是否将夹具纳入版本控制尚未决定。**另见 §8：最新一次 CI run 4/4 失败于 Build，未产生任何 suite 结果。**

**Windowed production rendering contract：dynamically verified**（Phase 5.2，`f25d4cb`）。在上述 committed baseline 上实跑 `EditorDemo`：真实窗口 + GL context + GP01 场景，viewport 实际渲染出非空像素，resize A→B→A 往返幂等。证据与边界见 `docs/Architecture-Runtime-Integration-Inventory.md` §4.7。

> **措辞边界**：此处**不**等于 renderer subsystem 已完成。PBR / shadow / formal RHI / backend switching **仍未接入产品路径**。另注：frame ownership 的严格阶段划分（`OnUpdate` → `Render3DScene` → `OnImGui`）**仍属 source reading**，动态证据仅覆盖"viewport 逐帧更新"与"SwapBuffers 生效"。

**当前门控不变**（本次不因 5.0 / 5.2 结果改变任何一项）：

| 项 | 状态 |
|---|---|
| formal RHI adoption decision | **已完成（2026-10-07）—— 否决**。`Core/RHI` 不作为 canonical production architecture，定位 sandbox / alternate backend surface（§9） |
| GL46 11 个空方法 | **不作为生产 defect**，且**明确不实现**（§9.3 / §9.4）—— 非 production requirement |
| headless → Avalonia presentation | 仍 **HRC-only / 断链**（§4.4），**不重开 P2** |
| Audio A1 | **冻结**（`AudioAssetManager` / `AudioClipManager` 归属未决）|
| HRC-3 | **不动**（scope / merge 由 owner 决定）|

**Audio Phase A**：A0 provisioning 已实现（`239cc5c`，**已验证但未进主线** —— 与 HRC-3 `Application.h/.cpp` 存在确定性同文件冲突，禁止 hunk 级整合）；A1 ownership migration **冻结**（`AudioAssetManager` 属 Stack 1 归属、`AudioClipManager` fallback 策略两项决策未决）。详见 `docs/Audio-Migration-Design-Freeze.md` §6.7。

---

## 1. 已决策的两项（2026-10-04 接受）

> 以下两节保留决策前的选项空间与依据。**结论已产出**：Audio = Stack 2 canonical；B4 = 显式受控 registration，bootstrap 不依赖 HRC-3；两项分别立项、不合并迁移。见各 ADR §0.5。

### 决策一：Audio canonical stack —— **已接受：Stack 2**

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

### 决策二：B4 ECS 组件注册模型 —— **已接受：显式受控 registration**

**要决定的问题**：ECS 侧组件是否需要预注册、以何种方式注册；以及 ECS 与 GameObject 两套组件模型是否收敛。

**关键歧义（尚未判定）**：`test_ecs` 曾观测到的失败究竟属于

- (a) **测试遗漏注册** —— `Position` / `Velocity` 本应是已注册组件
- (b) **测试正确表达了设计意图** —— ECS 确实不要求基类与预注册，`AddComponent<T>` 应让任意类型可用

**若为 (b)，则"拒绝未注册类型"这条守卫路径本身即为缺陷**：守卫只负责把未定义状态变成确定结果；若（b）成立，缺 meta 时应当就地补建而非拒绝。该未定义行为（Release 下 SEH）已由 `629ea47` 收敛，`EntityManager::AddComponentRaw` 也不再构造空签名 archetype（`assert(!metas.empty())` 现已不可达）。**但"拒绝"是否正确仍无法从代码得出。**

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

## 3. 已脱离上述决策、单独决定并实施的一项

**未注册组件类型在 Release 下导致 SEH，是否应改为可诊断的失败（增加运行期校验）？** —— **已决定：是。已实施于 `629ea47`。**

此项与"选哪种注册模型"**完全独立**（ADR §7 未决问题 3），可立即单独决定：

- 实际实施 → `AddComponentRaw` 在入口检测缺失 `ComponentMeta` 并拒绝该次添加，经既有 `Log::ErrorLoc` 上报；两处 copy 路径的空 meta 解引用改为守卫；`AddComponent<T>` 不再解引用 nullptr。守卫**不自动注册**，是否必须预注册仍由决策二回答。

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
确定 ECS 注册模型（决策二） | **已完成** —— 2026-10-04 接受显式受控 registration；bootstrap 边界同时裁定为不依赖 HRC-3 |
实施 B4 registration migration（已接受决策的第一个 slice） | **已完成** —— `89d58dc` + `734a9bd`，`test_ecs` 21/21 |
将 A0 provisioning（`239cc5c`）整合进主线 | 需 HRC-3 先收口：`Application.h/.cpp` 同文件确定性冲突，由 HRC owner 整合，**禁止 hunk 级整合** |
Audio A1 ownership migration | 冻结：需 `AudioAssetManager`（Stack 1 归属）与 `AudioClipManager`（fallback 策略）两项决策；且 A0 需先进主线 |
清理音频绕过路径 | 需先完成 Audio A1（Stack 2 决策已接受） |
引入 RelWithDebInfo/macOS/Linux-ASan CI 覆盖 | 需独立决策（当前 CI 已覆盖 Windows RelWithDebInfo+ASan，刻意未扩 macOS 与 Linux ASan） |

| 修复 CI Build 失败（`vulkan.h` / `spirv_cross.hpp`） | **两个 provenance 决策均已关闭**（B1 `387c66f`、A1 `6f5582e`，见 §8.7.5 / §8.7.6）。**但 §8 尚未整体关闭**：`VulkanCommon.h:13` 的 `vk_mem_alloc.h` 仍受 `Vulkan_FOUND` 门控，无 SDK 时 EngineCore 仍失败（**§8.8 OPEN**）。在 §8.8 与第 3 项 capability guard 决定前，不得再改 Vulkan/VMA 相关接线 |
| 决定 formal RHI 是否成为 canonical rendering architecture | **已完成（2026-10-07）** —— **否决**。`Core/RHI` 定位为 sandbox / alternate backend surface，不扩展为默认生产路径。详见 §9 |
| 实现 GL46 空 command methods / 补 `ShadowMapper` 子类 / 补 `CSMShadowMapper` header | **明确不做**（§9.4）—— 即使成本低也不做；不得以"追绿"或"未来会接入"为由启动 |

---

## 7. 本文档不做的事

- 不推荐任何架构选项
- 不判定 `test_ecs` 属于测试缺陷还是实现缺陷
- 不把"应显式注册"或"应自动注册"写成技术事实
- 不把观察项升级为 defect
- 不修改任何代码

---

## 8. CI dependency / build-boundary —— **PARTIALLY CLOSED（§8 整体仍 OPEN）**

**状态**：§8.7-B（B1）与 §8.7-A（A1）**均已关闭并实施**；但 **§8 整体未关闭** —— 关闭 host Vulkan discovery 后 `EngineCore` 仍在 VMA header 处失败（**§8.8 OPEN**）。本节记录事实与已完成的决策，**不含任何未获批准的方案**。

**来源**：CI run `37339412587`（Advanced C++ CI，head `1a066ed`，push 到 `master`）。

| job | 失败步骤 |
|---|---|
| ubuntu-latest, Release, gcc | **Build** |
| ubuntu-latest, Release, clang | **Build** |
| windows-latest, Release, cl | **Build** |
| windows-latest, RelWithDebInfo, cl, ASan | **Build ASan RelWithDebInfo shaderc artifact** |

**关键含义：该 run 未产生任何 suite 结果。** 4 个 job 全部止步于 Build / 依赖准备，`Test (full suite)` 一步都未执行。因此本次 CI **没有验证任何代码改动**，Linux/GCC 构建状态亦未被证明。

### 8.1 单一根因：两个缺失头同源于未声明的机器本地 Vulkan SDK

**本节已于 2026-10-06 更正**（见下方更正记录）。原先记为「两条独立缺口」，**该判断错误**。

**(a) 与 (b) 并非独立 —— 两者由同一个来源提供：**

| 缺失头 | 实际解析来源 | 路径 |
|---|---|---|
| `vulkan/vulkan.h` | 本机 Vulkan SDK | `C:/VulkanSDK/1.4.350.0/Include/vulkan/vulkan.h` |
| `spirv_cross/spirv_cross.hpp` | **同一个** Vulkan SDK | `C:/VulkanSDK/1.4.350.0/Include/spirv_cross/spirv_cross.hpp` |

两者均经 `engine/CMakeLists.txt:145` 的 `target_include_directories(EngineCore PRIVATE "${Vulkan_INCLUDE_DIRS}")` 进入编译行（该行在 `if(Vulkan_FOUND)` 内，见 `:131-134`）。权威证据：`build/Debug/CMakeCache.txt:958` `Vulkan_INCLUDE_DIR:PATH=C:/VulkanSDK/1.4.350.0/Include`；102 个 `.tlog` 含 `VulkanSDK`。

**因此：仅补 `Vulkan-Headers` submodule 不足以修复 CI** —— 它只解决 (a)，而 (b) 同样依赖该 SDK。

**(b) 本身仍然成立的部分（与 SDK 无关的事实）：**

- 代码 include `<spirv_cross/spirv_cross.hpp>`（`engine/src/Rendering/ShaderReflection.cpp:10`、`engine/src/Vulkan/VulkanPipelineLayoutCache.cpp:9`）
- pinned SPIRV-Cross `81fc2ea` 的头文件位于**仓库根目录**（26 个 root-level `spirv_*`）
- `include/spirv_cross/` 仅含 6 个文件（`barrier.hpp` `external_interface.h` `image.hpp` `internal_interface.hpp` `sampler.hpp` `thread_group.hpp`），**不含 `spirv_cross.hpp`**
- 仓库内**唯一**的 `spirv_cross/` 目录是 `third_party/spirv-cross/include/spirv_cross`，而**没有任何 CMakeLists 将它加入 include 路径**

### 8.2 「未解释的矛盾」—— 已解决

> **更正记录（2026-10-06）**：本节原标题为「未解释的矛盾（当前不实施修复的直接理由）」，并断言「CMake 声明的 include 路径无法解释本地这次成功」。**该断言错误，成因已查明。**

**成因**：`spirv_cross/spirv_cross.hpp` 由 **Vulkan SDK 的 include 目录**提供，经 `engine/CMakeLists.txt:145` 进入编译行。

**原探测为何漏掉**：先前只逐一探测了 `target_include_directories` 那一块里的 4 个目录（`box2d/include`、`third_party/spirv-cross`、`third_party/shaderc/libshaderc/include`、`third_party`），**遗漏了 `:145` 单独添加的 `${Vulkan_INCLUDE_DIRS}`**。即探测不完整，而非仓库存在矛盾。

**这同时推翻了「矛盾未解释 ⇒ 不得实施」的禁令前提。** 该禁令现仅保留其原始目的：不得在缺少 §8.4 决策的情况下改动依赖声明或编译语义。

### 8.2.1 新发现的次生问题：SPIRV-Cross 版本错配

头文件与被编译/链接的 `.cpp` 来自**两棵不同的树**：

| 文件 | 大小 | SHA-256 (前 16) |
|---|---|---|
| `C:/VulkanSDK/1.4.350.0/Include/spirv_cross/spirv_cross.hpp`（`ShaderReflection.cpp` / `VulkanPipelineLayoutCache.cpp` 实际使用） | 53957 | `503AD44A4BB5A2E7…` |
| `third_party/spirv-cross/spirv_cross.hpp`（`engine/CMakeLists.txt:170-181` 编译进 EngineCore 的 10 个 `.cpp` 所对应） | 55221 | `C8E4891D0F895A14…` |

内容不同、相差 1264 字节。**是否 ABI 兼容未经验证。** 同一模式亦适用于 Vulkan：`src/Vulkan/*.cpp` 对着 SDK 头编译，而 `third_party/vma`（pinned `3aa9212`）同样取 `${Vulkan_INCLUDE_DIRS}`（`third_party/CMakeLists.txt:104`）。

此项应并入 §8.6 的待决问题，不单独行动。

### 8.3 已定性的越界位置（事实记录，非方案）

`engine/src/Vulkan/*.cpp` 已被 `if(Vulkan_FOUND)` 正确守卫（`engine/CMakeLists.txt:131-134`）。越界的是**不在 Vulkan 目录下的文件无条件 include Vulkan 头**：

- `engine/include/Engine/Core/RHI/BindlessDescriptor.h:12`
- `engine/include/Engine/Vulkan/VulkanCommon.h:12`、`VulkanDeferredDeletion.h:15`、`VulkanFrameResource.h:12`
- `engine/src/Vulkan/VulkanDevice.cpp:20`（`vulkan_win32.h`）

`BindlessDescriptor.h` 位于 `Engine/Core/RHI/` 而非 `Engine/Vulkan/`，提示这里可能存在**更广泛的 RHI 头文件边界问题**。

### 8.4 明确未批准的方向（记录时不选）

| 方向 | 未批准的理由 |
|---|---|
| 补依赖声明（加 `Vulkan-Headers` submodule） | **只解决 §8.1(a)；(b) 同样依赖该 SDK**，故单做此项不足以让 CI 通过 |
| 调整 SPIRV-Cross pin 或仓库 layout | 在 §8.6 的版本错配问题有结论前，无法判断该改 pin、改 layout，还是改代码 include |
| 加 capability guard（缺依赖时移除/退化相关源码） | 这改变**编译语义** —— 「缺依赖时这些源码是否应从目标中消失」。需先有 Vulkan/RHI 的产品构建策略，而非 CI 层面的修补 |

**以上任一方案均未获批准，不得默认采用。**

### 8.5 明确禁止的临时手段

不得为了使 CI 变绿而：复制本机 SDK 内容进仓库、引入隐式环境依赖、手工改 include path，或以任何方式把**未经解释的本地行为差异**编码进仓库。

### 8.6 待决问题（待架构 / 构建策略决策）

> §8.6 第 1 / 4 项**已关闭**（第 1 项见 §8.7.6，第 4 项见 §8.7）。第 2 项已随 §8.7.5 关闭。第 3 项与 §8.8 仍 OPEN。

1. ~~Vulkan Headers provenance~~ → **已完成（2026-10-07，A1）**，见 §8.7.6。
2. ~~SPIRV-Cross provenance~~ → **已完成（2026-10-07，B1）**，见 §8.7.5。
3. 哪些非 Vulkan 路径的源文件需要 capability guard？`BindlessDescriptor.h` 被 3 个 committed 文件 include，说明该问题不限于 `Engine/Vulkan/`。**仍 OPEN。**
4. ~~§8.2 矛盾~~ → **已关闭**，见 §8.7。
5. **（新）** VMA 是否应随公共 Vulkan header 边界无条件可见？见 **§8.8**，**OPEN**。

### 8.7 §8.2 矛盾的关闭 + A / B 裁决准备

> **§8.2 证据级别：CLOSED — observed and experimentally reproduced.**
> `spirv_cross/spirv_cross.hpp` 的本地成功解析**唯一**依赖仓库外 Vulkan SDK 的 include 根；仓库 `third_party/spirv-cross` pin 的布局**无法**满足现有 include contract。
>
> **§8.2 已关闭 ≠ §8 已裁决。** 以下只提供决策依据，**不含推荐**。

#### 8.7.1 关闭 §8.2 的受控实验

同一探针（`#include <spirv_cross/spirv_cross.hpp>`）、同一套仓库内 include 目录，**只切换一个变量**：

| 变体 | include 集合 | 结果 |
|---|---|---|
**A** | 仅仓库内 7 个目录 | **`fatal error C1083`** |
**B** | A + `/external:I <SDK>/Include` | **EXIT=0**，`/showIncludes` 命中 `C:\VulkanSDK\1.4.350.0\Include\spirv_cross\spirv_cross.hpp` |
**C** | A + `/I <SDK>/Include`（普通 `/I`） | EXIT=0 |
**D** | 仅 `/I third_party/spirv-cross`，探针改为 `#include <spirv_cross.hpp>` | **EXIT=0，零 SDK 依赖** |

**由此排除**：PCH（去掉 `/Yu` `/Fp` `/FI` 后 A 仍必现 `C1083`）、隐式环境 / response file（`/showIncludes` 给出绝对路径）、"只是搜索顺序误判"（A 中 `/I third_party/spirv-cross` 存在且无效）。

**独立佐证**：MSBuild `EngineCore.tlog` 中 `SHADERREFLECTION.CPP` 的真实命令含 26 个 `/I` + **1 个 `/external:I C:/VulkanSDK/1.4.350.0/Include`**（即 `engine/CMakeLists.txt:145` 的 `${Vulkan_INCLUDE_DIRS}`）。

#### 8.7.2 A —— Vulkan Headers provenance 的决策依据

| 事实 | 值 |
|---|---|
需要**真** Vulkan API 头的 committed 位置 | **7 处**（`BindlessDescriptor.h:12`、`VulkanCommon.h:12`、`VulkanDeferredDeletion.h:15`×2、`VulkanFrameResource.h:12`、`VulkanDevice.cpp:20`、`RenderTestOffscreen.cpp:19`）|
守卫状态 | **仅 `src/Vulkan/*.cpp` 被 `if(Vulkan_FOUND)` 守卫**（`engine/CMakeLists.txt:134`）；而 `include/Engine/Vulkan/*.h` 在 `:66` **无条件 glob** |
传染面 | `VulkanCommon.h` 被 **16** 个 committed 文件 include，`VulkanFrameResource.h` **4** 个，`BindlessDescriptor.h` **3** 个 |
后果 | **无 SDK 时 `EngineCore` 无法编译** —— `message(WARNING "Vulkan backend disabled")`（`:150`）与实际行为矛盾 |
CI 现状 | `.github/` 中**无任何** `VULKAN_SDK` / vulkan 引用 |

**选项空间**：仓库自带（`Vulkan-Headers` submodule / vendored）／ 正式声明 SDK 为 CI+host 前置 ／ 对越界 header 加 capability guard。

> **已表达的倾向（记录，非本文档结论）**：倾向"仓库可声明、可复现的来源"，但须作为**正式决策**而非临时补 SDK。

#### 8.7.3 B —— SPIRV-Cross provenance 的决策依据

| 事实 | 值 |
|---|---|
代码 include contract | `<spirv_cross/spirv_cross.hpp>`、`<spirv_cross/spirv_glsl.hpp>`（**3 处**）|
pin `81fc2ea` 实际布局 | 根目录 **13** 个 `spirv_*.hpp`（含上述两个）；`include/spirv_cross/` 仅 **6** 个文件（`barrier` / `external_interface` / `image` / `internal_interface` / `sampler` / `thread_group`），**不含**所需头 |
被编译进 EngineCore 的 pin 源 | **10 个 `.cpp`**（`engine/CMakeLists.txt:169-181`）|
当前混合状态 | **pin 的 `.cpp` + SDK 的头**被组合链接 —— 即 §8.2.1 的版本错配（SDK 53957 B vs pin 55221 B，SHA 不同，ABI 兼容性未验证）|
**关键实验** | 变体 **D**：`<spirv_cross.hpp>` + 仅 `/I third_party/spirv-cross` → **EXIT=0，零 SDK 依赖**。即 **pin 本身自足**，是代码的 include 前缀把它绑到了 SDK |

**选项空间**：把代码 include 对齐 pin 真实布局（`<spirv_cross.hpp>`）／ 调整 pin 到匹配现有 contract ／ 统一到某一个 source。

> **明确排除的做法**：**"顺手加一个 include path"** —— 那会把未声明的 SDK 依赖固化为**隐式 source of truth**，且保留 pin-`.cpp` 与 SDK-头混用。

#### 8.7.4 A 与 B 必须分离裁决

二者是**独立 provenance 问题**，不得再打包为"缺依赖"：

- **A 决定 Vulkan API 头的来源**（影响 7 处、含 25 处传递 include，以及 §8.3 的 capability guard 问题）
- **B 决定 SPIRV-Cross 的 source/include contract**（影响 3 处 include 与 10 个被编译源）

**证据显示 B 存在不需要 SDK 的解**（变体 D），因此 **B 不必等待 A 的结论**；A 也不应因 B 而顺带决定。

#### 8.7.5 B —— **DECIDED：B1「代码契约对齐 pin」（2026-10-07）→ 已实施并验证**

**裁定**：接受 **B1** —— 把代码 include 由 `spirv_cross/spirv_cross.hpp` 改为 pin 实际提供的 `spirv_cross.hpp`；**pin 保持不变**，不再以 Vulkan SDK 的同名头作为补偿来源。

**依据**：变体 D 已直接证明零 SDK 可行；pin 本身自足，无理由为维持前缀而换 pin；保持 pin 不变避免引入新版本变化；**消除 pin `.cpp` + SDK 头混用**这一最危险状态；影响面（3 处 include）小于换 pin 或统一到另一 contract；不依赖 A 的裁定。

**明确排除（确保 B 的修改只解决 B）**：不加 SDK include path ／ 不改 SPIRV-Cross pin ／ 不复制 SDK 头 ／ 不加兼容 wrapper ／ 不顺手处理 Vulkan-Headers provenance。

**实施**（`387c66f`，2 文件 **3 行**，仅 include）：

| 位置 | 变更 |
|---|---|
`engine/src/Rendering/ShaderReflection.cpp:10` | `<spirv_cross/spirv_cross.hpp>` → `<spirv_cross.hpp>` |
`engine/src/Vulkan/VulkanPipelineLayoutCache.cpp:9` | `<spirv_cross/spirv_cross.hpp>` → `<spirv_cross.hpp>` |
`engine/src/Vulkan/VulkanPipelineLayoutCache.cpp:10` | `<spirv_cross/spirv_glsl.hpp>` → `<spirv_glsl.hpp>` |

**关闭门槛核验**：

| # | 门槛 | 结果 |
|---|---|---|
1 | 三处生产 include 已统一到 pin 真实 contract | **PASS** —— 全仓已无 `include <spirv_cross/`（0 处）|
2 | `third_party/spirv-cross` 仍是唯一 SPIRV-Cross source | **PASS** —— pin 未改动（`81fc2ea`），未新增任何 source |
3 | SPIRV-Cross 编译在**无 SDK 头参与**下成功 | **PASS** —— 变体 E（pin `/I` + SDK `/external:I` **同时存在**，即真实构建条件）`/showIncludes` 报告命中 `third_party\spirv-cross\spirv_cross.hpp`，且 include trace 中 **VulkanSDK 路径数为 0** |
4 | 不再出现 pin `.cpp` / SDK header 混用 | **PASS** —— 且**结构性消除**：SDK 提供的是 `spirv_cross\spirv_cross.hpp`（前缀形式），**无法**满足无前缀 `<spirv_cross.hpp>` |
5 | A 的 Vulkan-Headers 缺口仍单独保持 OPEN | **PASS** —— 未被本次改动掩盖，见下 |

**回归**：三配置（Debug / Release / RelWithDebInfo+ASan）构建 **0 error**；Debug **13/13** 全量 suite 通过，Release 与 RelWithDebInfo 各 5 个代表性 target **100% 通过**。

**A 未被掩盖（仍然 OPEN）**：7 处 committed 位置仍需真 Vulkan API 头；`include/Engine/Vulkan/*.h` 仍在 `engine/CMakeLists.txt:66` **无条件 glob**；`VulkanCommon.h` 仍被 **16** 个 committed 文件 include；无 SDK 时 `EngineCore` 仍无法编译；`.github/` 仍**零** vulkan 声明。

#### 8.7.6 A —— **DECIDED：A1 仓库声明式 Vulkan-Headers（2026-10-07）→ 已实施；§8.7-A CLOSED**

**裁定**：接受 **A1** —— 引入 pinned、可审计的 `Vulkan-Headers` source，使纯 Vulkan API headers 的 provenance 属于仓库声明的 dependency graph，而非依赖开发机恰好安装了某个 Vulkan SDK。

**实施**（`6f5582e`，4 文件 +30 行）：

| 项 | 值 |
|---|---|
pin | `8864cdc896bbc2a9b6eb36b3218fc9ef57908d77`，tag **v1.4.350**（**与项目开发所用 host SDK 同版本**，故 backend 启用时头/库零错配）|
接线 | `third_party/CMakeLists.txt` 新增 `VulkanHeaders` INTERFACE target（沿用既有 `vma` / `d3d12ma` 模式）|
消费 | `engine/CMakeLists.txt` **无条件**链到 `EngineCore`，且**刻意早于** `if(Vulkan_FOUND)` 内的 `${Vulkan_INCLUDE_DIRS}` → pinned 头始终优先于 SDK 头，保持单一 source of truth |

**未做（严格遵守边界）**：未解除 VMA 的 `Vulkan_FOUND` 门控 ／ 未改 `if(Vulkan_FOUND)` 定义 ／ 未重构 `Engine/Vulkan/*.h` 的 public-private 边界 ／ 未改 loader/linking ／ 未修 Vulkan backend 功能 ／ 未改 SPIRV-Cross（`387c66f` 已 CLOSED）。

**关闭门槛核验**（干净 worktree，`6f5582e`，`-DCMAKE_DISABLE_FIND_PACKAGE_Vulkan=ON` 强制无 SDK）：

| # | 门槛 | 结果 |
|---|---|---|
1 | Clean checkout provenance | **PASS** —— pinned gitlink `8864cdc`，可从仓库 dependency graph 重建 |
2 | No host-SDK dependency（**这些 Vulkan API 头**）| **PASS** —— 受控探针 `#include <vulkan/vulkan.h>` 仅用 pin：EXIT=0，include trace 中 **VulkanSDK 路径数 = 0** |
3 | **Vulkan API header closure**（见下方语义收紧）| **PASS** |
4 | No semantic widening | **PASS** —— 配置输出 `Vulkan SDK not found → Vulkan backend disabled`，backend 未被强行启用 |
5 | Backend separation preserved | **PASS** —— headers available ≠ backend available |
6 | Provenance documented | **PASS** —— pin / 来源 / 更新方式见本节与 `.gitmodules` |

> **门槛 3 语义收紧（2026-10-07）**：原表述为「CI 不需要隐式安装的 host SDK 来通过 build boundary」，该表述**已作废**，因为它会诱使把 §8.8 的相邻失败读成 A1 未完成。
>
> **收紧为**：*Vulkan API header closure —— 在 Vulkan package discovery disabled 时，EngineCore 所需的 **Vulkan API headers** 可由仓库声明的 Vulkan-Headers provenance 解析，不依赖 host SDK。*
>
> 按此定义 A1 **PASS**：7 处 `vulkan/vulkan.h` 全部由 pin 解析成功，编译错误已**前移**至 `VulkanCommon.h:13` 的另一条 unconditional include。

**§8.7-A 状态：CLOSED —— Vulkan API header provenance established.**

Pinned Vulkan-Headers `8864cdc` is the declared repository source for Vulkan API headers. Controlled no-SDK probing resolves `vulkan/vulkan.h` from the pinned dependency with zero SDK trace. Vulkan backend remains disabled when Vulkan SDK/package discovery is unavailable.

> **A1 关闭 ≠ 完整 Vulkan backend 可在无 SDK 环境构建。** 已证明的**仅是** API-header provenance。

### 8.8 VMA build-boundary —— **OPEN（独立于 §8.7-A）**

**不纳入 §8.7-A 关闭范围** —— 性质不同：`vulkan/vulkan.h` 是 **Vulkan API header provenance**（A 的对象）；`vk_mem_alloc.h` 是 **VMA 第三方 dependency header**。VMA 已有独立 pin `3aa9212`，其 provenance **不存在** host SDK 偷渡 source of truth 的问题。

**事实**：`engine/include/Engine/Vulkan/VulkanCommon.h:13` **无条件** include `vk_mem_alloc.h`，而 VMA 的 include/link 接线仍受 `if(Vulkan_FOUND)` 门控（`engine/CMakeLists.txt:144`）。因此在关闭 host Vulkan discovery 后，`EngineCore` **仍在 VMA header 处继续失败**：

```
engine/include/Engine/Vulkan/VulkanCommon.h(13,1): error C1083: 无法打开包括文件: "vk_mem_alloc.h"
```

**待决问题**：VMA 是否应随公共 Vulkan header 边界**无条件可见**（即把 `vma` 与 `VulkanHeaders` 同构地无条件链接），还是该边界本应重新界定？

**为何不在本轮决定**：解除该门控需要改变 `if(Vulkan_FOUND)` 的编译语义边界 —— 属本轮明确排除的架构/编译语义调整；且「再���一行就能绿」不构成扩大 A1 scope 的理由。

**因此**：`EngineCore` 目前**尚未**达到 host-SDK-independent；不得如此宣称。

---

## 9. Formal RHI adoption 决策 —— **Accepted：否决作为 canonical production architecture**

**裁定（2026-10-07）**：**否决。** `Core/RHI` **不成为**本仓库的 canonical production rendering architecture。

**否决的准确含义**：**不是**"RHI 没价值"，而是 —— **"RHI 当前不承担 canonical production rendering architecture 的职责。"** 它保留为 **sandbox / alternate backend surface**，并且不再让它看起来像"只差几个 TODO 就会成为生产 renderer"。

> **这是有意的架构收敛，不是语义削减。** 否决 formal RHI **不等于**获得 multi-backend support —— 后者仍是独立的产品/架构决策，且**不在本决策范围内**。当前生产路径继续是 **OpenGL**（`IGraphicsFactory` / `IRenderContext` / `IWindow`）。

### 9.1 决策前提（Phase 5.2 已入库）

Windowed production path **已动态验证**（inventory §4.7，`f25d4cb`）：真实窗口 + GL context + GP01 场景 + 非空像素 + resize 往返幂等。该路径走的是 `IGraphicsFactory` / `IWindow` / `IRenderContext`，**不是** `Core/RHI`。

### 9.2 否决理由（技术事实，非代码质量评价）

| 项 | 实测事实 |
|---|---|
契约规模 | `IRHIDevice` + `IRHICommandList` 共 **45 个纯虚函数** |
GL46 完成度 | 声明约 **50 个 override**，但 GL46 RHI 文件内约 **25 处为空体**；其中整个 command-list 核心操作有 **11 个空实现** |
**呈现能力** | `IRHISwapChain` 仅 5 个纯虚，**无 AA resolve、无 GPU timestamp、无窗口呈现**；`GL46SwapChain::Present(){}` / `Resize(){}` 均为空 —— **当前无法呈现** |
迁移性质 | 生产路径（`Application`/`EngineEditor`/`ViewportPanel`/`bridge`）对 `Core/RHI` 引用数为 **0** → 是**替换**一条已验证可用的抽象，而非接入缺失 |
既有规模 | `Core/RHI` 被 **175 个文件** include |

**结论依据**：接受 RHI 不是"补完几个 stub"，而是一条新的长期 rendering architecture migration；当前没有足够的产品收益证明应以一条尚未可生产的抽象替换一条已能真实出像素的抽象。

### 9.3 否决后的处置（正式记录）

| Surface | 处置 |
|---|---|
GL46 11 个空 command methods | **不作为当前生产 defect**（§7 原则：不把 UNREACHABLE 升级为 defect） |
`ShadowMapper` 无子类 | **非 production requirement** |
`CSMShadowMapper` 无 header（类定义在 `.cpp` 内） | **非 production requirement** |
Vulkan / D3D12 device 层 | **alternate / unreachable backend surface** |
`Core/RHI` 整体 | sandbox / alternate backend surface，可保留但**不扩展为默认生产路径** |

**并明确记录**：不再维持"未来默认会接入 production"的隐含承诺。

### 9.4 由此解除的工作

- **解除**：§6 中"等待 RHI 裁定"的门控（见 §6 该行更新）。
- **明确不做**（即使成本低也不做）：
  - 实现 GL46 那 11 个空 command methods 以"追绿"
  - 为 formal RHI 补 `ShadowMapper` 子类
  - 为 `CSMShadowMapper` 补 header
  - 为 RHI 重做已验证的 windowed path

### 9.5 仍维持不变的门控

`§0` 门控表其余全部维持：headless → Avalonia presentation 仍 HRC-only / **P2 不重开**、Audio A1 **冻结**、HRC-3 **不动**、Animation / ECS / Scripting 的 runtime admission **不由本决策派生**、CI §8 仍 **OPEN**。

**多后端需求**（若日后提出）须**另开独立决策**，不得由本决策自动派生。

**参与方**：引擎架构 + 渲染负责人 + 使用方（editor / bridge / sandbox）。

---
