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

> **⚠️ 本基线为 provisioned verification baseline，不是 bare fresh-checkout baseline（2026-10-08 强化）**
>
> `13/13` 与 `699` cases 是在**已 provision 上述 ignored fixture 的工作区**中取得的。真实 CI（run `37794358097`，clean checkout）证明：**committed tree 对 `test_bridge` / `test_gp01` / `test_content` 这 3 个测试不可自洽复现** —— 它们所需的 fixture 既无 committed 副本、也无 committed generator。
>
> 这**不是**"验证环境小问题"，而是已改变 **fresh checkout 能否复现 committed baseline** 这一基本事实的 **committed-tree reproducibility 缺陷**。已正式立项并归 **HRC owner**，见 **§8.10.3**。因此上述数字**不得**被读作"clean checkout 即可 13/13"。

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

| 修复 CI Build 失败（`vulkan.h` / `spirv_cross.hpp`） | **provenance chain CLOSED**：B1 `387c66f`、A1 `6f5582e`、VMA boundary `999d219`、capability guard `d14262a`、shaderc provisioning/activation `fed4326`、ASan 生成器 `66349f2`（**真实 runner 已验证**）。**但 §8 overall verification 仍 OPEN**，受**三个独立且已剥离出 provenance 门槛**的问题阻塞：① RHI sandbox Linux portability（§8.10.2，保留 all-target 红灯）；② committed-tree fixture reproducibility（§8.10.3，归 **HRC owner**）；③ MSVC ASan Windows interception failure（§8.10.4，**工具链/环境缺陷，不修仓库**）。**push 与 §8 整体 CLOSED 均需用户另行授权** |
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

## 8. CI dependency / build-boundary —— **PROVENANCE CHAIN: CLOSED；OVERALL VERIFICATION: OPEN**

### 状态冻结（2026-10-07）

> **§8 provenance chain：CLOSED**
> SPIRV-Cross → Vulkan-Headers → VMA header boundary → shaderc provisioning/activation：均已通过各自门槛，其中 shaderc 整条链已在**真实 runner** 上验证（Windows Release：provisioning / configure / build / `Verify SPIR-V activation` 全部 success）。
>
> **§8 overall verification：OPEN**
> 原因**不是** provenance，而是真实 CI 验证面仍受**三个独立问题**影响：
> ① **RHI sandbox Linux portability defect**（§8.10.2）
> ② **committed-tree fixture reproducibility defect**（§8.10.3）
> ③ **MSVC ASan Windows interception failure**（§8.10.4，工具链/环境缺陷，不修仓库）

**已关闭的 provenance 决策（不再重开）**：

| 决策 | 状态 | 关键证据 |
|---|---|---|
SPIRV-Cross provenance（B1）| **CLOSED** | `387c66f` / `60f5ecf` |
Vulkan-Headers provenance（A1）| **CLOSED** | `6f5582e` / `9e6e5be` |
VMA header boundary（§8.8）| **CLOSED** | `999d219` / `c17bc83` |
capability guard（§8.6-3）| **CLOSED** | `d14262a`（未新增 guard）|
shaderc provisioning / activation（§8.9）| **CLOSED** | `fed4326`，真实 runner 已验证 |
ASan artifact 生成器（§8.10.1）| **CLOSED — VALIDATED ON REAL RUNNER** | `66349f2`；artifact 生成 + configure + build 均成功 |

**剩余三个 OPEN 项的 owner / gate 关系**：

| 项 | 性质 | Owner | 是否属 §8 provenance 关闭门槛 |
|---|---|---|---|
RHI sandbox Linux portability | sandbox / alternate-backend **portability defect** | RHI sandbox surface 归属方 | **否 —— 已正式剥离** |
committed-tree fixture reproducibility | **repository reproducibility 缺陷** | **HRC owner** | **否** |
MSVC ASan interception failure | **工具链 / 环境缺陷** | toolchain / runner 环境 | **否** |

> **不得**把上述任一项记作"§8 provenance 回退"；**也不得**在三项未决时宣称 §8 整体 CLOSED。

**受控验证现状（本地，no-SDK，`-DREQUIRE_SHADERC=ON`，DEPS-pinned 依赖）**：`all`（全部 target 含 sandbox）exit=0 / 0 error；committed suite **13/13**（provisioned fixtures 下）；`sandbox/SpirVTest` **2/2**；RelWithDebInfo+ASan 下 `SpirVTest` **构建** exit=0 / LNK2038=0（**执行**受 §8.10.4 阻塞）。

**真实 CI 现状：RED。** 最新 run `37824159441`（`4f803a6`，已 push，ahead 0）。逐 job 归因：ubuntu gcc/clang → `Build`（§8.10.2）；windows Release → `Test`（§8.10.3）；windows RWD+ASan → `Verify SPIR-V activation`（§8.10.4）。**`66349f2` 已验证成功，不因整体红而回改。** `master` 未动（`1a066ed`）。

> **不得预先宣称 §8 CLOSED。** CI 最终验证须以**实际失败原因**记录，不得因本地复跑全绿而预判结论（见 §8.5：不得记录未选择、未执行的选项）。

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

> **§8.6 全部 5 项已关闭（2026-10-07）**：第 1 项见 §8.7.6（A1），第 2 项见 §8.7.5（B1），第 3 项见 §8.8.1，第 4 项见 §8.7，第 5 项见 §8.8。

1. ~~Vulkan Headers provenance~~ → **已完成（2026-10-07，A1）**，见 §8.7.6。
2. ~~SPIRV-Cross provenance~~ → **已完成（2026-10-07，B1）**，见 §8.7.5。
3. ~~哪些非 Vulkan 路径的源文件需要 capability guard~~ → **已关闭（2026-10-07）**：committed compile graph 中**未观察到独立 defect**，未新增任何 guard，见 §8.8.1。
4. ~~§8.2 矛盾~~ → **已关闭**，见 §8.7。
5. ~~VMA 是否应随公共 Vulkan header 边界无条件可见~~ → **已决策并实施（2026-10-07）**：header 无条件、backend 条件，见 §8.8。

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

### 8.8 VMA build-boundary —— **DECIDED（2026-10-07）→ 已实施；§8.8 CLOSED**

**不纳入 §8.7-A 关闭范围** —— 性质不同：`vulkan/vulkan.h` 是 **Vulkan API header provenance**（A 的对象）；`vk_mem_alloc.h` 是 **VMA 第三方 dependency header**。VMA 已有独立 pin `3aa9212`，其 provenance **不存在** host SDK 偷渡 source of truth 的问题。

**裁定：VMA compile-time header availability 与 VulkanHeaders 同属公共 Vulkan API surface；VMA runtime / backend linkage 仍由 Vulkan capability 控制。**

**依据**：`engine/include/Engine/Vulkan/VulkanCommon.h:13` **无条件** include `vk_mem_alloc.h`，而该文件属于 `engine/CMakeLists.txt:66` **无条件 glob** 的 public header 集合。故 VMA 头必须在 Vulkan package discovery 关闭时仍可解析 —— 否则会形成不一致模型：`vulkan/vulkan.h` provenance 已独立，同一 public header surface 却仍偷偷要求 `Vulkan_FOUND`。

**实施**（`999d219`，`engine/CMakeLists.txt`，+14 行，**纯增量**）：

| 项 | 内容 |
|---|---|
header availability | `if(TARGET vma)` → `target_link_libraries(EngineCore PUBLIC vma)`，与 `VulkanHeaders` 块同处、置于 Jolt 之前 |
backend linkage | **未动** —— `if(Vulkan_FOUND)` 内的 `target_link_libraries(EngineCore PUBLIC Vulkan::Vulkan vma)`、`target_include_directories(... "${Vulkan_INCLUDE_DIRS}")`、`target_compile_definitions(... ENGINE_HAS_VULKAN)`、`VULKAN_SOURCES` **逐字节未改** |

**刻意未做**：**未**把现有 `vma` 接线块整体搬出 `if(Vulkan_FOUND)`。新增链接为独立语句，原条件块内的 `vma` 链接变为冗余但无害（CMake 去重），从而 **backend 门控语义零变化**。

**关闭门槛核验**（干净 committed worktree `999d219`，`-DCMAKE_DISABLE_FIND_PACKAGE_Vulkan=ON`，cache 中 `Vulkan_*` 条目 **0**）：

| # | 门槛 | 结果 |
|---|---|---|
1 | 无 SDK 时 `vk_mem_alloc.h` 可由 pinned VMA 解析 | **PASS** —— 配置 `Vulkan SDK not found → backend disabled`；`Vulkan_*` cache 条目 0；include trace 中 pinned `Vulkan-Headers` ×3、pinned `vma` ×1、**`VulkanSDK` 路径 0** |
2 | Vulkan backend 仍 disabled | **PASS** —— 配置输出仍为 `Vulkan backend disabled`；`Vulkan_LIBRARY` 不在 cache |
3 | 不改 loader / link semantics | **PASS** —— 条件块逐字节未改；无 `Vulkan::Vulkan` 参与 |
4 | fresh checkout 通过 EngineCore 该 compile boundary | **PASS** —— `EngineCore + 13 committed targets` **Debug build exit=0，0 error**（约 6 min）；`ctest` **13/13 通过** |
5 | 文档区分 VMA header provenance/visibility 与 Vulkan backend capability | **PASS** —— 即本节上述二分 |

> 门槛 4 期间出现 **1 次 `test_content` 失败**（首轮 ctest 12/13），复跑 3 次全 suite 均 13/13，隔离复跑 6/6 通过。判定为**既有的 `test_content` 不稳定观察项**，与 §8.8 无因果关系，不构成本次关闭的失败。

**§8.8 状态：CLOSED。**

> **边界重申（防止后续误读）**：VMA **header 可见性** 是无条件的；Vulkan **backend capability** 仍是条件化的。**VMA 头可见 ≠ Vulkan backend 可用 ≠ Vulkan backend 被启用。**

#### 8.8.1 §8.6-3（capability guard）—— **CLOSED（2026-10-07）：committed compile graph 中未观察到独立的 capability-guard defect**

**CLOSED — no independent capability-guard defect observed in the committed compile graph.**
With Vulkan package discovery disabled and no host SDK available on the include path, EngineCore and all 13 committed targets build successfully and the full committed test suite passes. The previously identified `BindlessDescriptor.h` reachability concern does not manifest as an independent compile-boundary failure in the current committed graph. No capability guard was added because doing so would change compile semantics without a reproduced defect.

**核心证据**：`CMAKE_DISABLE_FIND_PACKAGE_Vulkan=ON`、无 host SDK（cache 中 `Vulkan_*` 条目 0）的条件下，仍完成 **`EngineCore + 13 targets` build 0 error + ctest 13/13**（证据级别见 §8.8 门槛 4）。该证据强于任何单点探针：它覆盖的是**完整 committed compile graph**，而非孤立 TU。

**判定原则**：

* **存在 include ≠ 当前存在需要 guard 的错误边界。** `BindlessDescriptor.h` 被 3 个 committed 文件 include 是**结构性事实**，不是已复现的失败。
* 此前所有观察到的 header failure（`vulkan/vulkan.h` → `vk_mem_alloc.h`）均已由 A1 / §8.8 的 **provenance 修复**消解，**未留下任何需要 guard 才能通过的残留边界**。
* 现在新增 capability guard 将属于**为预防一个尚未被证实的未来边界而修改 compile semantics** —— 当前证据**不足以**支持这一步。

> **边界重申（防止后续误读）**：本项关闭**并非**证明 Vulkan backend 无条件可用；它只证明**当前 committed compile graph 不需要**为了修复一个已观察到的 header failure 而新增 guard。Vulkan backend 的可用性仍取决于 host / CI 的 Vulkan package 前置条件，与 §8.7.6、§8.8 的 backend capability 门控一致。

### 8.9 Shaderc provisioning + activation gate —— **OPEN（独立于 Vulkan/VMA provenance；2026-10-07 新开）**

> **本项不是 Vulkan/VMA provenance 的延伸。** §8.7-A / §8.8 关闭的是 **header provenance 与 header 可见性**；本项是 **third-party artifact 的取得（provisioning）与激活（activation）**，两者性质不同、失效模式不同。

**问题定性**：CI 的验证面**静默偏离**本地，且**无人察觉**。

**已观测事实**：

| 事实 | 证据 |
|---|---|
CI `Build` 步骤无 `--target` → 构建 `all` | `cmake-multi-platform.yml:124-125` |
本地 `all` 构建失败 LNK2038 | `shaderc_combined.lib(shaderc.obj)` 值 1 vs `SpirVTestApp.obj` 值 0 |
但 **CI 不会**撞上 LNK2038 | `engine/CMakeLists.txt:240` 要求 Debug **与** Release 预编译 shaderc **同时**存在才启用 shaderc；二者被 gitignore（shaderc submodule 的 `build/`）|
**无任何 committed 脚本或 CI 步骤产出该二者** | `tools/` 下仅有 `build_shaderc_asan_rwd.ps1`，它只产出 `build-asan-rwd/RelWithDebInfo`（且仅 Windows）|
Linux 亦无 shaderc | `cmake-multi-platform.yml:88-95` 的 apt 列表不含 shaderc；CMake 用硬编码路径，`find_package` 不参与 |
CI 因此以 SPIR-V **静默禁用**通过 | 受控模拟：隐藏两个预编译 lib → `Shaderc lib not found → GLSL→SPIR-V compilation disabled` |
**且 suite 无法察觉该能力丢失** | 受控模拟：**13 targets build 0 error + ctest 13/13** —— 与 shaderc **开启**时结果**相同** |

**关键含义**：CI 绿灯**不代表**验证了 SPIR-V 编译路径。当前 SPIR-V 前置条件既**不明确**也**未被满足**，与 §8 的原始目标（*deterministic fresh checkout + 明确依赖前置，不依赖隐式搜索*）相悖。

**裁定（2026-10-07）—— 选 (c)，接受 CI 时长成本**：

* **Provisioning**：CI 必须实际取得与**消费端配置匹配**的 shaderc。
* **Activation guard**：CI **不得**允许 shaderc 缺失后静默退化为 `ENGINE_HAS_SHADERC=OFF` 仍然绿灯。
* **验证目标**：CI 绿灯必须**证明** SPIR-V 路径被启用并实际参与构建/验证。
* **LNK2038**：属**同一 artifact/configuration contract** 问题，用**匹配的 shaderc 重建**解决；**不**单独改 `SpirVTest` 或链接逻辑。
* **平台实现**：Windows 按 Debug/Release 的 CRT/配置匹配；Linux 按其对应构建配置匹配，**不机械复制** `/MDd`/`/MD`。

**关闭门槛**（至少）：

1. shaderc artifact 在 CI 中**可重建**；
2. `ENGINE_HAS_SHADERC` **明确 enabled**；
3. SPIR-V target **实际编译/链接**；
4. **缺失 artifact 时 CI 明确失败**，而非降级；
5. Windows **LNK2038 消失**；
6. 留下**可审计的启用证据**。

#### 8.9.1 §8.9 —— **已实施并验证（`a86694c`）；门槛 1–6 全部 PASS，§8.9 CLOSED**

**LNK2038 根因（实证，非推测）**：

| 侧 | 实际值 |
|---|---|
消费侧（`sandbox/SpirVTest` 等） | Debug=`MultiThreadedDebugDLL`(/MDd)、Release=`MultiThreadedDLL`(/MD) —— 与根 `CMakeLists.txt` 策略一致 ✓ |
原预编译 shaderc | `CMAKE_MSVC_RUNTIME_LIBRARY:UNINITIALIZED=MultiThreadedDLL` —— **单值强制 /MD，Debug config 亦被覆盖** ✗ |

即 Debug 消费者用 `/MDd` 而 Debug artifact 用 `/MD`。属**同一 artifact/configuration contract** 问题，已按裁定用**匹配重建**解决，**未**改动 `SpirVTest` 或链接逻辑。

**实施**（`a86694c`，3 文件 +337/−3）：

| 项 | 内容 |
|---|---|
Provisioning | 新增 `tools/build_shaderc_ci.ps1`（220 行）：按**各平台自身**配置生成 Debug+Release。Windows 启用 shared CRT 且**刻意不强制** `CMAKE_MSVC_RUNTIME_LIBRARY`，交由 multi-config 生成器逐配置取值；Linux **不套用** Windows CRT 概念，改用 Ninja Multi-Config 以保持相同目录布局 |
Artifact 路径 | `engine/CMakeLists.txt` 改为 **platform-aware**（原为 Windows `.lib` 硬编码 → **任何其它平台该门永远无法满足**，SPIR-V 永久不可达）|
Activation guard | 新增 `-DREQUIRE_SHADERC`（**缺省 OFF**，本地行为不变；CI 传 ON → 缺失即 `FATAL_ERROR`）|
可审计证据 | configure 打印 activation 状态与**逐配置实际解析到的** artifact 路径 |
CI 接线 | provisioning 前置于 configure（`EXISTS()` 在 configure 期求值）+ `actions/cache` + `-DREQUIRE_SHADERC=ON` + Build 之后新增 **Verify SPIR-V activation** 三重校验 |

**门槛核验**（干净 worktree，`-DREQUIRE_SHADERC=ON`，无 host SDK）：

| # | 门槛 | 结果 |
|---|---|---|
1 | artifact 在 CI 中可重建 | **PASS** —— 脚本 Debug 约 3–4 min、Release 约 6.5 min 生成，路径与门查找一致；**幂等**（复跑复用树，不重复构建）|
2 | `ENGINE_HAS_SHADERC` 明确 enabled | **PASS** —— configure 输出 `Shaderc activation: ENABLED (REQUIRE_SHADERC=ON)` 并逐配置列出三条 artifact 路径 |
3 | SPIR-V **实际**编译/链接 | **PASS** —— `sandbox/SpirVTest` **exit=0**，`[PASS] TestSpirvCrossReflection` + `[PASS] TestShaderCompileAndReflect`，**2 passed / 0 failed**，即真实完成 GLSL→SPIR-V 编译并反射 |
4 | 缺失 artifact 时明确失败 | **PASS（已观测失败路径）** —— 仅备 Debug 时 configure **exit=1**，`FATAL_ERROR` 点名两个缺失路径并给出 provisioning 命令 |
5 | Windows LNK2038 消失 | **PASS** —— `all`（含 `SpirVTest`）**exit=0，errors=0，LNK2038=0**；Debug artifact 现为 `MultiThreadedDebugDLL` |
6 | 可审计启用证据 | **PASS** —— configure 输出 + CI `Verify SPIR-V activation` 三项断言 |

**同轮通过的关联验证**：`all`（全部 target，含 sandbox）**exit=0 / 0 error**；committed suite **13/13**。

> **门槛 3 的精确界定（避免后续误读）**：SPIR-V 编译**确由** `sandbox/SpirVTest` 在无 SDK 环境下真实执行并通过。但引擎自身的 `engine/src/Vulkan/ShaderCompiler.cpp` 仍位于 `if(Vulkan_FOUND)` 门控的 `src/Vulkan/` 内（`ShaderCompiler.obj` 在本次构建中计数 0），故**引擎管线内**的 SPIR-V 使用在 CI（无 Vulkan SDK）下**仍不被编译**。二者是不同层面，本项关闭的是 **artifact 取得 + 激活 + 实际编译/链接可验证**。

**两次测试不稳定观察（非本项因果，留档）**：

| 出现 | 结果 |
|---|---|
首轮 `test_content` 失败（§8.8 期间）| 复跑 3 次全 suite 13/13；隔离 6/6 通过 |
本项首轮 `test_core` 失败 | 隔离 **43/43 通过**；复跑 2 次全 suite 均 13/13 |

两次均为**首次运行失败、复跑全绿**的既有不稳定模式，**与 shaderc provisioning / activation 无因果关系**（SPIR-V 开关两种状态下 suite 结果相同，见 §8.9 事实表）。**留作观察项，不据此开启新缺陷**，但已记录为重复出现。

#### 8.9.2 真实 CI 归因（run `37754073786` / `37756712866`）—— **两个根因已修，第三个 OPEN**

**前提更正**：§8.9.1 的"门槛 1–6 全部 PASS"其准确范围是**本地 Windows + 本地 cmake 3.30.3 + 已填充的 shaderc 树**，**不可外推到 runner**。以下为真实 runner 证据。

**run `37754073786`（`f2b49bf`）**：4/4 job **全部**止步于 `Provision shaderc artifacts`，`Configure`/`Build`/`Test` **一次都未执行**。两个不同根因：

| 平台 | 现象 | 根因 | 状态 |
|---|---|---|---|
Linux (gcc/clang) | `SPIRV-Tools was not found - required for compilation`（shaderc `third_party/CMakeLists.txt:85`）| **误判已更正**：原以为是"嵌套 submodule 未 init"，故提议 `git submodule update --init --recursive`。**该提议会是 no-op** —— shaderc `7060a66` **无 `.gitmodules`**，其 index 中 spirv-tools/glslang/spirv-headers **只有 LICENSE 文件、无 gitlink**。本地之所以从未暴露，是那些目录**此前已被填充**（本地一直在用 shaderc）| **OPEN（sourcing 决策）** |
Windows (2 jobs) | `Generator Visual Studio 17 2022 → could not find any instance of Visual Studio` | 脚本**硬编码** `-G "Visual Studio 17 2022"`；runner 镜像已迁移，**默认生成器为 `Visual Studio 18 2026`**，`vswhere` 仅返回 `C:\Program Files\Microsoft Visual Studio\18\Enterprise`。即硬编码的工具链版本，不是工具链本身的问题 | **已修（`57d0ad4`）**：不再传 `-G`，交由 cmake 选默认（与主工程 configure 一致；VS 系仍 multi-config，目录布局不变）|

**同轮修复**：诊断步骤自身在 Windows 上变成 fatal —— pwsh 7.5 起 `$PSNativeCommandUseErrorActionPreference` 默认 `$true`，叠加 GitHub `pwsh` shell 预置的 `$ErrorActionPreference='stop'`，使原生命令非零退出会终止 job。已显式关闭两者并加 `continue-on-error`（`57d0ad4`）。

**run `37756712866`（`57d0ad4`）**：诊断步骤 4/4 通过；4/4 仍止步于 provisioning，但**错误已收敛为单一根因**：

```
SPIRV-Tools was not found - required for compilation
```

Windows 日志确认 `-- Building for: Visual Studio 18 2026`、`cl.exe 14.51.36231` **成功选定** —— 生成器问题确已解除，不再掩盖后续失败。**四个 job 现在因同一原因失败**。

**因此 §8.9 的"门槛 1–6 全部 PASS"必须撤回为"本地条件通过，runner 未通过"**，并新增 OPEN 决策项：**shaderc 依赖来源（sourcing）** —— 固定的 shaderc commit 并不自包含，其 SPIRV-Tools / glslang / spirv-Headers **既不在该 commit 内、也未声明为 submodule**，故 fresh checkout **无法从源码构建 shaderc**。

#### 8.9.3 B → B′ 决策更正 —— **B 字面形式不可实现；B′ 已接受并实施**

**B（原定义）**：把 shaderc 重新 pin 到**自带 `.gitmodules`**、可由 recursive-submodule 机制完整重建的 commit。

**静态审计结论：不存在这样的 commit。** `google/shaderc` 的 `.gitmodules` 在**所有**相关 ref 均不存在：

| ref | `.gitmodules` |
|---|---|
`main` / `master` | 不存在 |
`v2023.5` / `v2024.4` / `v2025.1` | 不存在 |
当前 pin `7060a66` | 不存在 |

**shaderc 上游从不使用 git submodule 承载这三个依赖。** 实际机制是 committed **`DEPS`** 清单 + **`utils/git-sync-deps`**（与 SPIRV-Tools/glslang 同一模式）。

**B′（已接受）**：**保持 shaderc pin `7060a66` 不变**，以其 committed `DEPS` + `utils/git-sync-deps` 作为 transitive dependency provenance 与 fresh-checkout materialization contract。

标准并非放宽，而是**从"使用 submodule"纠正为真正目标**：

> **dependency provenance 必须由上游仓库的 committed manifest 声明，并可由 fresh checkout 按固定 revision 重建。**

`DEPS` 中的固定 revision：

```
spirv_tools_revision:   f589ef005c49f6f19c8e78eb5269104ba293beb4
glslang_revision:       2ee090f606ace31e07f584b1c1b9ddf4909ce202
spirv_headers_revision: 942fe4b988359a0750b79f0ae7ed735994d3147d
```

**满足"否决 A 的理由"**：pin 的 owner 是 **shaderc 自身**，我们的脚本**不含任何 revision**，只调用上游已提交的同步器 —— 因此 `repo pin → committed DEPS → upstream tool → 固定 revision` 的 provenance 链可审计，与 §8.7-A / §8.8 的"依赖由仓库声明"原则同构。

**审计中发现的 provenance 缺陷（必须作废旧证据）**：本地 `spirv-tools`/`glslang`/`spirv-headers` 的 revision 与 `DEPS` **全部不一致**：

| 依赖 | 本地（旧环境） | `DEPS` pin |
|---|---|---|
spirv-tools | `d5bbf95d87dd` | `f589ef005c49` |
glslang | `ce138e2c2d69` | `2ee090f606ace` |
spirv-headers | `daa093dd29aa` | `942fe4b98835` |

故 §8.9.1 的门槛证据属**旧环境证据，不得迁移**。

**实施**（`build_shaderc_ci.ps1`）：configure 之前**调用上游 `utils/git-sync-deps`**，并**先丢弃**既有 transitive 依赖与 build 产物，从 `DEPS` pin 做 fresh materialization；随后校验三个依赖目录确实生成并打印其 HEAD revision。**未**改 shaderc pin、**未**在脚本写入任何 revision、**未**加 system shaderc fallback、**未**改 `REQUIRE_SHADERC`、**未**改 consumer API、**未**动 Vulkan/VMA 接线。

**B′ 验证链（从 clean `DEPS` 状态重跑，不继承旧证据）**：

| 步骤 | 结果 |
|---|---|
fresh materialization | **PASS** —— 三个依赖落到 `DEPS` 精确 revision（`f589ef005c49` / `2ee090f606ac` / `942fe4b98835`）|
重建 Debug/Release | **PASS** —— 462.2 MB / 66.2 MB（与旧环境 458.1 / 65.5 MB **不同**，佐证依赖图确已更换）|
CRT contract | **PASS** —— Debug=`MultiThreadedDebugDLL`(/MDd)、Release/MinSizeRel/RelWithDebInfo=`MultiThreadedDLL`(/MD) |
`all` 构建（含 `SpirVTest`） | **PASS** —— exit=0、0 error、**LNK2038=0** |
`SpirVTest` | **PASS** —— exit=0，`[PASS] TestSpirvCrossReflection` + `[PASS] TestShaderCompileAndReflect`，**2/2** |
committed suite | **PASS** —— 连续 2 次 **13/13** |

> **验证环境事故留档（非代码缺陷）**：本轮验证 worktree 曾在长时构建中途消失，导致后续命令静默回落到主仓库，并在主仓库生成 `build/bp-prime`（已删除）。首轮 suite 出现 `test_content` / `test_gp01` / `test_bridge` 三项失败，经查为**该 worktree 未 provision 12 个 gitignored fixtures**，补齐后三者均通过且全 suite 13/13。**与 pinned 依赖图无因果关系**。另有一处环境失误：`robocopy /XD build` 大小写不敏感，误排除 `JoltPhysics/**Build**/`，导致 Jolt 目标未生成 —— 已改为精确排除路径。**两处均属验证环境问题，已修正，未进入任何 commit。**

---

### 8.10 真实 CI 后续暴露的三个既有缺陷（§8.9 之后独立处理）

真实 runner 证据见 §8.9.2 / §8.9.3。run `37794358097`（`fed4326`）中，**shaderc 整条链已在 Windows Release 上验证通过**（provisioning / configure / build / `Verify SPIR-V activation` 全部 success）。失败项全部是**此前从未被验证到的既有缺陷**，与 provenance 决策无关。

| # | 缺陷 | 现象 | 状态 |
|---|---|---|---|
8.10.1 | `build_shaderc_asan_rwd.ps1` 硬编码 `Visual Studio 17 2022` | Windows RelWithDebInfo+ASan 止步于该步骤 | **已修（`66349f2`，真实 runner 已验证）** |
8.10.2 | `engine/src/RHI/RHIWindow.cpp` 为结构性 Win32-only | Linux gcc/clang 止步于 `Build` | **OPEN（按硬门槛停止实施）** |
8.10.3 | 3 个测试依赖的 fixture 无仓库内来源 | Windows Release `Test` 3 项失败 | **OPEN（归 HRC owner）** |
8.10.4 | MSVC ASan Windows interception failure | Windows RelWithDebInfo+ASan 止步于 `Verify SPIR-V activation` | **OPEN（定性为工具链/环境缺陷，不修仓库）** |

#### 8.10.1 ASan 脚本生成器 —— **已修并验证**

与 §8.9.2 中 `build_shaderc_ci.ps1` **同一根因**（硬编码生成器版本），按同类修复移除 `-G`，交由 cmake 选默认生成器。**未**重新定义成 provenance 问题。

验证（本地，`-SkipReconfigure`，与 CI 调用一致）：

| 步骤 | 结果 |
|---|---|
脚本生成 RWD+ASan artifact | **PASS** —— exit=0，**257.8 MB**，脚本自带守卫确认 `__asan_*` 符号存在 |
主工程 configure 消费该 artifact | **PASS** —— `RelWithDebInfo -> third_party/shaderc/build-asan-rwd/libshaderc/RelWithDebInfo/shaderc_combined.lib (/MD + ASan)` |
RWD+ASan 构建 `SpirVTest` | **PASS** —— exit=0、0 error、**LNK2038=0**、exe 生成 |

#### 8.10.2 RHI sandbox Linux portability defect —— **OPEN；已从 §8 provenance 关闭标准中正式剥离**

**性质**：**sandbox / alternate-backend portability defect**，由 §9 formal RHI 决策（否决 canonical 采纳、保留为 sandbox / alternate surface）定性。**不是**简单的 include 缺口或 CMake 缺口 —— 新核查证明它是**真正的 Win32-only 实现**。

**硬门槛检查结果（未通过，故未实施修复）**：`engine/src/RHI/RHIWindow.cpp` 无条件使用 native Win32：

| 行 | 内容 |
|---|---|
L22 | `#define GLFW_EXPOSE_NATIVE_WIN32` —— **无条件**，无 `#ifdef` |
L23 | `#include <GLFW/glfw3native.h>` —— 无条件（Linux 下 L105 `#include <windows.h>` 直接编译终止）|
L57 | `m_HWND = glfwGetWin32Window(m_Window);` —— **无条件**，该成员是 D3D12 swapchain 的 HWND |

裁定明确禁止"为了过 Linux 强行换头"，并要求此时**停止在小修复上扩大范围**，记为 RHI sandbox 的独立平台缺陷，**不得偷偷引入新的平台抽象**。故本轮**未改动该文件**。

> **后果（须明示）**：Linux gcc/clang 两个 job 在 `Build` 仍会失败，**本轮无法转绿**。这是既有 portability defect 的如实暴露，不是回退。
> **不得**用 (a) 平台 guard 把它永久掩盖 —— 那会把"源文件能否编译"变成新的 guard 语义。

**明确禁止的处置方式**（本轮均未做）：加 `#ifdef _WIN32` 把它"修绿"；换成跨平台 GLFW API 假装解决；为 CI 绿而删除或屏蔽该 target。

**正式剥离声明**：本项**不计入** §8 provenance chain 的关闭标准。它是独立于 §8 的 sandbox portability 缺陷，**不代表 Vulkan / shaderc provenance chain 回退**。

> **独立 CI scope 决策（当前不做）**：是否把 RHI sandbox 从 `all`-targets gate 拆为非门禁 job，属**独立的 CI scope 决策**，**不在 §8 内顺手完成**。本轮**不**调整任何 gating。
> Linux all-build 在此决策作出前应**继续诚实暴露**该缺陷。

**Owner / gate 关系**：Owner = RHI sandbox surface 的归属方（随 §9 一并保留为 sandbox）；Gate = 不阻塞 §8 provenance closure；**但**在处置决定作出前，Linux CI 保持红。

#### 8.10.3 committed-tree fixture reproducibility defect —— **OPEN；正式立项，归 HRC owner**

**性质**：**repository reproducibility 缺陷**，**不是**验证环境小问题 —— 它已改变 **fresh checkout 能否复现 committed baseline** 这一基本事实。

**已记录事实**：committed tree 中 `test_bridge`、`test_gp01`、`test_content` 所需的 12 个 ignored fixtures **既无 committed 副本、也无 committed generator**（`sandbox/AssetBakery` 只处理 `--shaders` / `--materials`，不产出其中任何一项；全仓无 committed 源码写入 `Main.scene` / `content_scratch`）。**因此 clean checkout 不能自洽复现此前的 13/13、699-case baseline。**

**明确拒绝的处置方式**：CI 步骤从 HRC 工作区复制 assets —— 那会把**隐式工作区状态伪装成 CI provisioning**，正是本项要消除的东西。

**§0 连带更正（保持）**：**13/13、699 cases 是 provisioned verification baseline，不是 bare fresh-checkout baseline。**

**HRC owner 的合法关闭方向（仅此两类）**：

1. 把这些 fixture 或**可追溯的生成机制**纳入 HRC 交付；
2. 让这些 committed tests **不再依赖工作区外的 HRC-3 内容**。

**归属**：这属于 **HRC ownership / repository reproducibility**，**不是** §8 dependency strategy 的临时修补。**本节不代为决定。**

#### 8.10.4 MSVC ASan Windows interception failure —— **OPEN；定性为工具链 / 环境缺陷，不修仓库**

> **⚠️ 定性更正（2026-10-08）**：本项此前被定性为「**ASan runtime deployment 缺失**」，并曾提议部署 `clang_rt.asan_dynamic-x86_64.dll`。该定性**已被直接实验推翻**，现更正为 interception failure。

**事实链（真实 runner，run `37824159441` / `4f803a6`）**：`66349f2` 修好 generator 后，**RWD+ASan artifact 成功生成 → CMake configure 成功 → Build 成功**；`Verify SPIR-V activation` 的**前两项亦通过**（`REQUIRE_SHADERC=ON` 已记录；Debug 467.0 MB / Release 194.1 MB 均在）。失败仅在**第 3 项执行 `SpirVTest.exe`**：

```
interception_win: unhandled instruction at 0x7fffaf8928c0: 80 3a 00 4c 8b d1 75 04
AddressSanitizer: CHECK failed: interception_win.cpp:193
  "(("Interception failure, stopping early ... && 0)) != (0)"
```

**有界只读调查的三个答案**：

| 问题 | 答案 | 证据 |
|---|---|---|
**Q1** 是否由宿主注入模块 / 加载顺序触发 | **是（环境侧）** —— ASan 在**初始化期**无法 hook 某个系统模块。`probe.exe` 与 `SpirVTest.exe` 的 fault address **完全相同**（`0x7fffaf8928c0`，落在系统 DLL 地址区间），说明二者命中**同一个未被 hook 的系统模块**，与可执行体自身无关 |
**Q2** 同一 toolchain 下最小独立 ASan exe 是否重现 | **是，稳定重现** —— 3 行 `main`、**零仓库代码 / 零 shaderc / 零 engine / 零第三方**，同一 MSVC **14.44.35207**、`/MD /fsanitize=address`，报**一字不差**的同一 interception CHECK 失败。**故仓库侧被排除** |
**Q3** 是否只出现在 runner/CI 沙箱，而非仓库构建产物 | **不是仓库产物问题** —— 失败发生在 **`main()` 之前**（probe 未打印 `PROBE_REACHED_MAIN`），即 ASan runtime/interception 初始化期。任何仓库代码都无法影响该阶段 |

**结论（按裁定分叉的第一支）**：

> **最小 probe 亦稳定失败 → 定性为工具链 / runner 环境缺陷，OPEN，不修仓库。**

**边界遵守**：调查全程**只读**。未部署任何 DLL（实验用 DLL 已删除，`clang_rt` 计数复原为 0）；未 CI 化旁路；未禁用 ASan；未改链接 flags；未改验证语义；**本节不含任何源码或 CI 改动**。

**保留的诊断事实（禁止 CI 化）**：`ASAN_WIN_CONTINUE_ON_INTERCEPTION_FAILURE=1` 下程序可继续执行，并打印 `SPIRV-Cross: integrated` / `Shaderc: LINKED` —— 证明 **Shaderc / SPIR-V / Reflection 本身正常**。但该变量**关闭 ASan 自身的保护守卫**，把红变绿的同时降低真实检查强度，**只能作为诊断证据，禁止作为 CI 修复**。

**调查的一处诚实限制**：最小 probe 在**本地主机 + 同一 toolset** 上执行，**未**直接在 GitHub runner 上运行（调查期间不改 CI）。但由 Q1/Q2/Q3 组合可确定：**仓库侧已被排除**，无论 runner 侧的具体注入来源为何。

**`66349f2` 状态：VALIDATED ON REAL RUNNER** —— 其目标缺陷（VS17→VS18 generator）已被 artifact 生成 + configure + build 实际证明。**不回改。**

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
