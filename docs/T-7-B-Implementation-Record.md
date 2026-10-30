# T-7-B — OpenAL EFX Implementation Record

**状态**：`T-7-B — PASS — SCOPE-BOUND / CLOSED`
**最终状态**：`T-7-B — CLOSED — SCOPE-BOUND`（2026-10-30）
**日期**：2026-10-30
**上游**：`docs/T-7-B-Implementation-Plan.md`、`docs/T-7-B-API-Design.md`、
`docs/T-7-Audio-EFX-Scope.md`
**授权**：T-7-B Gate B 实施；不覆盖 Git 提交、Stack 1/A0/A1、CPU DSP 接入、
Bridge/Editor ABI 或 `third_party/openal-soft` 修改。

> 本文件记录实施与证据，不自行宣布 T-7-B `PASS`，不创建最终关闭裁决。

---

## 1. 实施范围

| 工作包 | 实施 |
|---|---|
| B2 Capability | Init Ready 后查询 `ALC_EXT_EFX`、入口可用性、`ALC_MAX_AUXILIARY_SENDS`；缓存 capability；Shutdown/Re-Init 清除并重查 |
| B3 Reverb | 创建标准 `AL_EFFECT_REVERB`；冻结参数；设置/读回；销毁；Handle 失效 |
| B4 Slot | 创建 Auxiliary Effect Slot；一对一 Attach/Detach；依赖检查；销毁 |
| B5 Routing | Source Aux Send index 0；内部 Send Gain Filter；send level finite + 钳制；断开；Source 销毁清理 |
| B6 Low-pass | 创建 `AL_FILTER_LOWPASS`；`gain` / `gainHF`；读回；Direct Filter 绑定/解绑/销毁 |
| B7 Lifecycle | EFX 资源随 Engine generation 失效；Shutdown 依赖顺序；Failed 清理；跨 Engine 拒绝 |

### 1.1 关键设计决定

- **非拥有 Handle**：`AudioEfxHandle` 含 engineId/generation/serial/type，是值类型；
  没有 owning wrapper 析构路径，因此析构不会调用 AL。显式 `DestroyEfxObject` 或
  Engine Shutdown 释放 native 资源。
- **内部 Send Filter**：每个 Source 路由使用独立的内部 Low-pass Filter
  (`AL_LOWPASS_GAIN = sendLevel`, `AL_LOWPASS_GAINHF = 1.0`)；不复用 Slot Gain。
  该内部 Filter 不暴露公共 Handle，由 Source Route/Deferred Release/Shutdown 清理。
- **Low-pass 语义**：公共 API 只提供 `gain` / `gainHF`，不提供伪精确的 `cutoffHz`。
- **一对一最小模型**：一个 Slot 一个 Reverb；一个 Reverb 一个 Slot；一个 Source
  一个 Aux Send 和一个 Direct Filter。
- **错误语义**：复用 `AudioStatus`；EFX Unsupported 不影响基础音频；EFX BackendError
  在 Context 仍有效时保持 Engine Ready；设备丢失按 T-7-A 进入 Failed。
- **对象数量**：不设硬编码上限；由后端 allocation 决定；失败返回明确错误。

### 1.2 改动文件

新增：

- `engine/include/Engine/Core/Audio/IAudioEfx.h`
- `docs/T-7-B-Implementation-Plan.md`
- `docs/T-7-B-API-Design.md`
- `docs/T-7-B-Implementation-Record.md`
- `docs/T-7-B-Test-Record.md`

修改：

- `engine/include/Engine/OpenAL/OpenALBackend.h`
- `engine/src/OpenAL/OpenALBackend.cpp`
- `engine/src/OpenAL/OpenALAudioState.h`
- `engine/include/Engine/OpenAL/OpenALAudioEngine.h`
- `engine/src/OpenAL/OpenALAudioEngine.cpp`
- `tests/test_audio/OpenALRuntimeTest.cpp`

**未修改**：Stack 1、`AudioClip`、`AudioSystem`/`AudioSourceComponent`、
`bridge/`、`editor_avalonia/`、`capi.h`、`third_party/openal-soft`。

---

## 2. 测试与构建证据

### 2.1 测试结果

| 配置 | 结果 | 证据 |
|---|---|---|
| Debug `test_audio` | **145/145 PASSED** | `build/t7b_test_audio_debug_run7.log` |
| Release `test_audio` | **145/145 PASSED** | `build/t7b_test_audio_release_run4.log` |
| Debug ctest：`test_audio/test_bridge/test_hrc` | **3/3 PASSED** | `build/t7b_ctest_debug3.log` |
| Release ctest：`test_audio/test_bridge/test_hrc` | **3/3 PASSED** | `build/t7b_ctest_release3.log` |
| 真实设备 EFX smoke | **PASS**（未 SKIP） | 上述 Debug/Release test_audio 日志 |

145 = 112 既有 + 16 A-T（A-T01～A-T16）+ 15 EFX 定向测试 + 2 真实设备 smoke。
EFX 定向测试包含 E-T01～E-T11、E-T14 及 3 个 B2 补充用例（E-T02B/C/D）；
E-T12 由完整 `test_audio`/CTest 回归覆盖，E-T13 由 E-T02（Unsupported + 基础播放）
覆盖。详细映射见 `docs/T-7-B-Test-Record.md`。

真实设备上 EFX capability 为 supported/routingSupported，`OpenALDeviceEfxSmoke`
完成 Reverb/Slot/Source/Send/Direct Filter/Shutdown 全路径，未触发 SKIP。

### 2.2 构建结果

| 配置 | 结果 | 证据 |
|---|---|---|
| Debug 全解决方案 | 0 error | `build/t7b_full_debug_build3.log` |
| Release 全解决方案 | 0 error | `build/t7b_full_release_build3.log` |

警告口径：最终 Debug/Release 全解决方案构建各出现 2 条既有 LNK4006
（`RHI::IsBackendAvailable` / `RHI::GetDefaultBackend` 重复符号）警告；未观察到新增
编译警告。本记录**不宣称零警告或 clean build**。

### 2.3 回归

- T-7-A 已接受的 112 既有 + 16 A-T 用例全部包含在 145 中并通过。
- `test_bridge`、`test_hrc` Debug/Release 均通过。
- 未修改旧测试断言；只扩展了 Fake 后端与新增 EFX 测试。

---

## 3. 契约符合性摘要

| 契约要求 | 实现 | 证据 |
|---|---|---|
| EFX 能力检测/缓存/Re-Init 重查 | `InitializeEfxCapabilities` + `GetEfxCapabilities` | E-T01/E-T02B/C/D |
| Unsupported 不影响基础音频 | capability Unsupported；EFX 创建不调用后端 | E-T02/E-T13 |
| 入口缺失返回 BackendError | capability BackendError；Base Ready | E-T02B |
| Reverb 创建/参数/读回/销毁 | `CreateReverb` / Set/Get / Destroy | E-T03 |
| Slot 一对一 Attach/Detach/销毁 | Slot registry + dependency checks | E-T04 |
| Source aux send / send level | internal Send Gain Filter；clamp | E-T05 |
| Low-pass gain/gainHF + Direct Filter | filter registry + bindings | E-T06 |
| 非 Owner Thread 拒绝 | `CheckEfxReady` 先线程检查 | E-T07 |
| Shutdown 依赖顺序 | routes → direct → filters → detach → effects → slots | E-T08 |
| DeviceLost fail-closed | Engine Failed；EFX 不再调用后端 | E-T09 |
| BackendError 不误升级 | Context 有效时 Engine Ready | E-T10 |
| Source 销毁清理 EFX 关系 | `CloseSourceEfx` + Deferred Release hook | E-T11 |
| 跨 Engine/旧 generation 拒绝 | engineId/generation/serial/type | E-T14 |
| 不新增 Bridge/Editor ABI | 未修改对应目录/ABI | 工作树范围 |
| 不改 Stack 1/CPU DSP/openal-soft | 未修改 | 工作树范围 |

---

## 4. 残余风险 / 未验证

- **非拥有 Handle**：公共 EFX Handle 没有 owning wrapper 析构；未显式 `DestroyEfxObject`
  的公共对象会保留到 Engine Shutdown。内部 Send Filter/Route 由 Source 销毁、
  Deferred Release 或 Shutdown 清理。此设计避免 destructor 中调用 AL，但与“每个
  public EFX wrapper 自带析构”模型不同，已在 B1 API 冻结记录中明确。
- **EFX Provider 差异**：不同 OpenAL Provider 的 EFX 参数读回、aux send 上限与
  资源数量可能不同；本轮真实设备 smoke 通过，但不能外推到所有 Provider/设备。
- **未运行 ASan/TSan**：未宣称 sanitizer 或并发 race-free。
- **未实现**：Echo/Chorus、复杂效果链、Slot 共享/串联、CPU DSP 接入播放链、
  Listener environment 到 EFX、Bridge/Editor 音频、专用音频线程/命令队列。
- **Stack 1 / Legacy Bypass**：仍冻结，不在 T-7-B 强制边界内。
- **`third_party/openal-soft`**：未修改；真实 EFX 支持由当前 pinned 构建提供，
  未审计其内部实现。
- **未执行 Git 写入**：无 commit/push/tag/merge/reset/clean/stash。

---

## 5. 阶段状态

| 阶段 | 状态 |
|---|---|
| T-7-A | `PASS — SCOPE-BOUND / CLOSED` |
| T-7-B Scope | `SCOPE FROZEN` |
| T-7-B Gate B | `IMPLEMENTATION AUTHORIZED` |
| B0～B9 实施 | `PASS — SCOPE-BOUND / CLOSED` |
| T-7-B 最终验收 | `PASS — SCOPE-BOUND / CLOSED` |
| Bridge ABI / Editor 音频 | `NOT AUTHORIZED` |

> 最终验收已由裁决方于 2026-10-30 确认；正式关闭记录见 `docs/T-7-B-Closure-Record.md`。
