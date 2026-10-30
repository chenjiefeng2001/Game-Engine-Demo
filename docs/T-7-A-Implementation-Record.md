# T-7-A — Audio Runtime Safety & Lifecycle Implementation Record

**状态**：`T-7-A — PASS — SCOPE-BOUND / CLOSED`
**最终状态**：`T-7-A — CLOSED — SCOPE-BOUND`（2026-10-30）
**日期**：2026-10-30
**验收依据**：裁决方依据本实施记录与报告结果作出；不代表裁决方独立读取本地源码、测试日志或重新运行测试。
**契约**：`docs/T-7-Audio-GateA-Contract.md`（`FROZEN`）
**授权**：用户已单独授权 T-7-A Gate B；实现范围仅限 Stack 2 Runtime Safety & Lifecycle。
**未执行**：EFX、A0/A1 迁移、Stack 1 功能修改、Bridge/Editor 音频、专用音频线程、
`third_party/openal-soft` 修改、commit/push/tag/merge/reset/clean/stash。

> 本文件是实施/证据记录，不是新的契约，也不改变 Gate A 的任何条款。

---

## 1. 实施范围

### 1.1 已完成工作包

| 工作包 | 实施内容 | 对应契约 |
|---|---|---|
| A1 Owner Thread | `OpenALAudioEngine::Init()` 绑定 Owner Thread；所有 Stack 2 入口在 AL 前检查线程；非 Owner 返回 `WrongThread` 且不产生后端调用 | D7-2 / §3、§4 |
| A2 Engine State | `Uninitialized/Ready/Failed/ShuttingDown/Shutdown` 状态机；重复 Init 幂等；Failed 后必须 Shutdown 才能重试；重复 Shutdown 幂等 | D7-4 / §3 |
| A3 Resource Lifetime | Engine 拥有 source/buffer native 资源；wrapper 持有 generation；非 Owner 析构只登记 Deferred Release；Shutdown 在 context 销毁前清理；Buffer binding 阻止提前删除 | §5 |
| A4 Failure Handling | 新增 `AudioStatus`；创建失败返回 `nullptr`，不返回 native ID 0 伪对象；非 Owner/错误状态返回明确状态 | §6 |
| A5 Device Failure | 通过 `ALC_EXT_disconnect` 查询设备连接；断开进入 `Failed`；无扩展时核心后端失败按 fail-closed 处理；不自动恢复 | D7-4 / §7 |
| A6 Shutdown | 状态置 `ShuttingDown`；停止/解绑/删除 source，再删除 buffer，再销毁 context/device；重复 Shutdown 无后端调用 | §5.4 |

### 1.2 关键实现

- `AudioDefs.h`：新增 `AudioStatus` 与 `AudioEngineState`。
- `AudioEngineRuntime`（`engine/src/OpenAL/OpenALAudioState.h`）：Owner Thread、状态、
  generation、source/buffer registry、buffer binding、Deferred Release 队列。
- `IOpenALBackend`（`engine/include/Engine/OpenAL/OpenALBackend.h`）：
  内部确定性后端 seam；生产实现 `OpenALBackend.cpp` 调用真实 AL/ALC；测试使用 fake。
- `OpenALAudioEngine`：实现状态机、owner 检查、资源创建/销毁、设备故障、Shutdown；
  不再在析构函数中直接操作 OpenAL。
- `OpenALAudioSource`：所有播放/停止/参数操作经 owner/state/generation 检查；
  析构只登记延迟释放，不调用 AL；支持 buffer binding 计数。
- `OpenALAudioBuffer`：创建/加载经 owner/state/generation 检查；析构不调用 AL；
  buffer 绑定期间禁止显式删除。
- `IAudioEngine/IAudioSource/IAudioBuffer`：增加状态/结果返回与内部失效接口；
  既有调用方忽略返回值的写法仍可编译；未新增 Bridge ABI。

### 1.3 改动文件

修改：

- `engine/include/Engine/Core/Audio/AudioDefs.h`
- `engine/include/Engine/Core/Audio/IAudioEngine.h`
- `engine/include/Engine/Core/Audio/IAudioSource.h`
- `engine/include/Engine/Core/Audio/IAudioBuffer.h`
- `engine/include/Engine/OpenAL/OpenALAudioEngine.h`
- `engine/include/Engine/OpenAL/OpenALAudioSource.h`
- `engine/include/Engine/OpenAL/OpenALAudioBuffer.h`
- `engine/src/OpenAL/OpenALAudioEngine.cpp`
- `engine/src/OpenAL/OpenALAudioSource.cpp`
- `engine/src/OpenAL/OpenALAudioBuffer.cpp`
- `tests/CMakeLists.txt`
- `tests/test_audio/ListenerTest.cpp`（仅 FakeAudioEngine 接口适配）

新增：

- `engine/include/Engine/OpenAL/OpenALBackend.h`
- `engine/src/OpenAL/OpenALBackend.h`
- `engine/src/OpenAL/OpenALBackend.cpp`
- `engine/src/OpenAL/OpenALAudioState.h`
- `tests/test_audio/OpenALRuntimeTest.cpp`

**未修改**：`engine/src/Audio/AudioEngine.cpp`、`AudioSource.cpp`、`AudioClip.cpp`、
`AudioSystem.cpp`、`AudioSourceComponent.cpp`、Stack 1 头文件、`bridge/`、
`editor_avalonia/`、`capi.h`、`third_party/`。

---

## 2. 测试与证据

### 2.1 T-7-A 测试

`tests/test_audio/OpenALRuntimeTest.cpp` 覆盖：

- A-T01 正常 Init / Owner 绑定
- A-T02 重复 Init 不重复 device/context
- A-T03 错误线程调用（source 操作、CreateSource、Shutdown）在 AL 前拒绝
- A-T04 错误线程资源释放延迟到 Owner，不在错误线程触发 AL
- A-T05 Shutdown 顺序（stop → delete source → delete buffer → context → device）
- A-T06 外部 Source 晚于 Engine Shutdown 不调用 AL
- A-T07 Buffer 创建失败返回明确失败并清理
- A-T08 Source 创建失败返回明确失败
- A-T09 Device Disconnect 进入 Failed
- A-T10 Failed 后播放/创建被拒绝且无后端调用
- A-T11 重复 Shutdown 幂等
- A-T12 Init 失败后必须 Shutdown 才能重试
- A-T13 Source 持有 buffer；绑定期间禁止删除；Stop 后按序释放
- A-T14 外部 wrapper 晚于 Engine 对象析构不调用 AL
- A-T15 Shutdown/Re-Init 后旧 wrapper 永久 inert
- A-T16（补充）跨 Engine source/buffer 身份校验，拒绝 foreign resource

**A-T16 追溯说明**：A-T16 是 Gate A 原始矩阵 A-T01～A-T15 之外的补充覆盖，不属于生产
范围扩张。其目的为验证跨 Engine 的 `IAudioSource` / `IAudioBuffer` 身份校验，防止
foreign resource 被本 Engine 误销毁，或被 Source 误绑定。对应契约条款：
Gate A §5.1（Engine 拥有 native source/buffer）、§5.3（wrapper / resource 失效语义），
并强化 A3/A4 的资源所有权与失败保护。A-T16 不新增冻结契约条款。

另有 `OpenALRuntimeDeviceSmoke`：在真实默认 OpenAL 设备上执行 Init/Create/Play/Stop/
Update/Shutdown。本轮设备可用，测试 **PASS**，未 SKIP。

### 2.2 测试结果

| 配置 | 结果 | 证据 |
|---|---|---|
| Debug `test_audio` | **129/129 PASSED**（112 既有 + 17 T-7-A） | `build/t7a_test_audio_debug_run5.log` |
| Release `test_audio` | **129/129 PASSED**（同上） | `build/t7a_test_audio_release_run4.log` |
| Debug ctest：`test_audio/test_bridge/test_hrc` | **3/3 PASSED** | `build/t7a_ctest_debug_final3.log` |
| Release ctest：`test_audio/test_bridge/test_hrc` | **3/3 PASSED** | `build/t7a_ctest_release_final3.log` |

设备 smoke 在 Debug/Release 均通过；没有把 SKIP 计为 PASS。

### 2.3 构建结果

| 配置 | 结果 | 证据 |
|---|---|---|
| Debug 全解决方案 | 0 error | `build/t7a_full_debug_build_final3.log` |
| Release 全解决方案 | 0 error | `build/t7a_full_release_build_final3.log` |

警告口径：EngineCore 重新链接时观察到 **2 个既有 LNK4006**（`RHI::IsBackendAvailable` /
`RHI::GetDefaultBackend` 重复符号）警告；该警告也出现在 T-7-A 之前的构建日志中
（本轮可见于 `build/t7a_test_audio_debug_build7.log` / `...release_build4.log`）。
最后一轮增量构建日志未出现 warning 行，但本记录**不宣称零警告或 clean build**。

---

## 3. 契约符合性摘要

| 契约要求 | 实现 | 证据 |
|---|---|---|
| 单 Owner Thread、无专用音频线程/队列 | Init 绑定线程；所有 Stack 2 操作在 AL 前检查 | A-T01/A-T03 |
| 非 Owner 在 OpenAL 前拒绝并返回明确状态 | `AudioStatus::WrongThread`；backend 调用计数为 0 | A-T03 |
| Shutdown 在 Owner Thread | 非 Owner Shutdown 返回 WrongThread；测试确认无 context 销毁 | A-T03/A-T11 |
| Engine 状态机 | Uninitialized/Ready/Failed/ShuttingDown/Shutdown | A-T01/A-T02/A-T09/A-T12 |
| Init 失败不产生伪 Ready | 失败清理并进入 Failed；必须 Shutdown 再 Init | A-T12 |
| Source/Buffer 创建失败不返回 ID 0 伪对象 | 返回 nullptr；失败 native 资源清理 | A-T07/A-T08 |
| 资源删除在 context 有效时完成 | Shutdown 顺序先 source→buffer→context→device | A-T05 |
| 非 Owner 析构不调用 AL | Deferred Release；Owner Update/Shutdown 处理 | A-T04 |
| 外部 wrapper 晚于 Engine 不访问已销毁 context | generation/state 检查；析构 no-op | A-T06/A-T14/A-T15 |
| Buffer 绑定期间不提前删除 | binding 计数；`DestroyBuffer` 返回 InvalidState | A-T13 |
| 设备断开 fail-closed | `ALC_EXT_disconnect` 查询；进入 Failed；不自动恢复 | A-T09/A-T10 |
| 不实现 EFX / 不恢复 A0/A1 / 不新增 Bridge/Editor ABI | 未触碰对应文件/接口 | Git 工作树与构建范围 |

---

## 4. 未做 / 残余风险

- **Legacy Bypass 未修复**：`AudioSystem`、`AudioSourceComponent`、`AudioClip` 以及
  Stack 1 的裸 AL 路径仍在 T-7-A 强制边界之外；`AudioSourceComponent::Play(nullptr)`
  的历史 no-op 仍存在。它们不属于 T-7-A 验收范围，未来须由 A1 或独立迁移处理。
- **Stack 1 未删除/未迁移**：`Engine::Audio::AudioEngine`/`AudioSource` 保持冻结。
- **无专用音频线程/命令队列**：仅实现 Thread Confinement；不宣称一般并发安全。
- **设备故障策略**：有 `ALC_EXT_disconnect` 时查询连接；无该扩展时把核心后端失败
  视为 fail-closed（Failed）；不自动恢复，需显式 Shutdown → Init。
- **Engine 在非 Owner Thread 析构**：为防止错误线程调用 AL，析构记录错误并将状态置为
  Shutdown，不执行 backend cleanup；这是安全降级，但可能留下 native 资源。正常使用要求
  Engine 在 Owner Thread 销毁/Shutdown。
- **EFX 不存在**：T-7-B 范围已冻结，但未实现；本记录不声称任何 EFX 能力。
- **设备/EFX 真实性**：本轮设备 smoke 通过；EFX 能力未查询；EFX 验收需 T-7-B。
- **未运行 ASan/TSan/并发压力**：T-7-A 采用单 Owner Thread 约束，测试断言拒绝而非
  数据竞争压力；不宣称 race-free 并发。
- **未新增 Bridge/Editor ABI**：编辑器音频集成和音频 Inspector 未授权。

---

## 5. 保护边界与 Git 状态

- `third_party/openal-soft` 未修改、未更新、未重置、未清理；既有脏项保持。
- 未修改 Stack 1 功能；未实现 EFX；未接入 Application/Editor/Bridge。
- 未新增 Bridge ABI；`capi.h` 未变。
- 未执行 commit、push、tag、merge、reset、clean 或 stash。
- 本次改动仅为 T-7-A 授权的 Engine OpenAL Stack 2、相关接口与 `test_audio`。

---

## 6. 阶段状态

| 阶段 | 状态 |
|---|---|
| T-7 Gate 0 | `PASS — READ-ONLY COMPLETE` |
| T-7 Gate A | `FROZEN`（契约未改） |
| T-7-A Runtime Safety | `PASS — SCOPE-BOUND / CLOSED` |
| T-7-B EFX | `SCOPE FROZEN — IMPLEMENTATION DEFERRED` |
| Bridge ABI / Editor 音频 | `NOT AUTHORIZED` |

> 最终验收已由裁决方于 2026-10-30 确认；本记录保留实施、证据与验收条款。
> （历史状态：T-7-B 当时为延后；后续已实施并关闭，见 `docs/T-7-B-Closure-Record.md`。）

---

# T-7-A — Final Acceptance

**Decision:** PASS — SCOPE-BOUND / CLOSED

## Accepted implementation

- A1: Owner Thread binding and wrong-thread operation rejection.
- A2: Engine state machine and Init/Shutdown transitions.
- A3: Native resource registry, Deferred Release and generation/epoch isolation.
- A4: AudioStatus error semantics and invalid resource creation protection.
- A5: Fail-Closed device failure handling within supported provider capabilities.
- A6: Owner Thread resource cleanup and idempotent Shutdown behavior.

## Verification evidence

- Debug `test_audio`: 129/129 passed.
- Release `test_audio`: 129/129 passed.
- Relevant CTest suites: 3/3 in Debug and Release.
- Full solution builds: zero errors in Debug and Release.
- Real device smoke: passed in both configurations.
- Existing LNK4006 warnings remain documented.

The 16 A-T tests include A-T01 through A-T16. A-T16 is supplemental coverage beyond the
original A-T01 through A-T15 matrix; its purpose and contract mapping remain documented
in §2.1 of this record.

## Accepted limitations

- Stack 1 remains frozen and is not migrated or removed.
- Legacy Bypass is not claimed to be generally thread-safe.
- Active device-disconnect detection depends on provider capabilities.
- Non-Owner Thread Engine destruction is a restricted safe-degradation path, not proof of
  complete resource cleanup.
- ASan and TSan were not run.

No claim is made that all OpenAL providers, Windows devices, concurrent usage patterns or
legacy interfaces have been validated.

## Protected boundaries

- Stack 2 remains the sole canonical development path.
- No dedicated audio thread or command queue.
- No A0/A1 migration restart.
- No EFX implementation.
- No Stack 1 feature expansion.
- No Bridge ABI or Editor audio integration.
- `third_party/openal-soft` remains unchanged.
- No commit, push, tag or merge was performed.

**Final status:** T-7-A — CLOSED — SCOPE-BOUND.

T-7-B EFX remains independently deferred and requires separate authorization.

**Acceptance basis:** Supplied implementation record and reported test/build results.
No independent repository inspection or test execution is claimed.
