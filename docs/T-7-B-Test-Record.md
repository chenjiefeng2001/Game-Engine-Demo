# T-7-B — OpenAL EFX Test Record

**状态**：`T-7-B — PASS — SCOPE-BOUND / CLOSED (TEST EVIDENCE ACCEPTED)`
**日期**：2026-10-30
**依据**：`docs/T-7-B-API-Design.md`、`docs/T-7-B-Implementation-Record.md`
**范围**：E-T01～E-T14、T-7-A 回归、基础音频回归、真实设备 EFX smoke。
**未执行**：ASan/TSan、所有 OpenAL Provider/设备矩阵、Git 写入。

---

## 1. 测试数量

`test_audio` 总计 **145** 个用例：

- 112 个既有音频用例；
- 16 个 T-7-A A-T 用例（A-T01～A-T16）；
- 15 个 EFX 定向测试（含 3 个 B2 补充用例）；
- 2 个真实设备 smoke（基础音频 + EFX）。

| 配置 | 结果 | 证据 |
|---|---|---|
| Debug `test_audio` | **145/145 PASSED** | `build/t7b_test_audio_debug_run7.log` |
| Release `test_audio` | **145/145 PASSED** | `build/t7b_test_audio_release_run4.log` |

没有 SKIP；真实设备 smoke 在 Debug/Release 均通过。

---

## 2. E-T01～E-T14 映射

| ID | 测试/证据 | 结果 |
|---|---|---|
| E-T01 | `OpenALEfxTest.E_T01_CapabilitySupported` | PASS |
| E-T02 | `E_T02_UnsupportedKeepsBaseAudioWorking` | PASS |
| E-T03 | `E_T03_ReverbLifecycleAndReadback` | PASS |
| E-T04 | `E_T04_AuxiliarySlotLifecycle` | PASS |
| E-T05 | `E_T05_SourceAuxRoutingAndSendLevel` | PASS |
| E-T06 | `E_T06_LowPassFilterLifecycleAndBinding` | PASS |
| E-T07 | `E_T07_WrongThreadEfxCallsAreRejected` | PASS |
| E-T08 | `E_T08_ShutdownReleasesEfxDependenciesInOrder` | PASS |
| E-T09 | `E_T09_DeviceLostRejectsEfxWithoutBackendCalls` | PASS |
| E-T10 | `E_T10_EfxBackendErrorKeepsEngineReady` | PASS |
| E-T11 | `E_T11_SourceDestructionClosesEfxRoutes` | PASS |
| E-T12 | 完整 `test_audio` 145 + CTest 3/3 | PASS |
| E-T13 | `E_T02_UnsupportedKeepsBaseAudioWorking` + E-T02B/C/D | PASS |
| E-T14 | `E_T14_CrossEngineAndOldGenerationEfxHandles` | PASS |

### 2.1 补充覆盖

| 测试 | 内容 |
|---|---|
| `E_T02B_EfxEntryPointFailureIsBackendError` | 扩展存在但必要入口不可用 → BackendError；基础 Engine Ready |
| `E_T02C_NoAuxiliarySendMakesRoutingUnsupported` | `maxAuxiliarySends=0` → effects 可用、routing Unsupported |
| `E_T02D_CapabilityIsRequeriedAfterReInit` | Shutdown/Re-Init 后重新查询 EFX capability |
| `OpenALDeviceEfxSmoke.InitConfigurePlayShutdown` | 真实设备 EFX 全路径 smoke |

E-T12 的“基础音频回归”由完整 `test_audio` 145/145 与 CTest
`test_audio/test_bridge/test_hrc` 3/3 覆盖；没有为 EFX 修改任何既有断言。

---

## 3. EFX 定向断言摘要

- **E-T01**：capability supported/routingSupported/max send 正确；入口检查一次；
  Reverb 创建成功。
- **E-T02**：扩展 Unsupported；基础 Init 成功；EFX 创建失败且 `genEffectCalls == 0`；
  Source/Buffer/Play/Stop 仍工作。
- **E-T03**：Reverb 默认构造成功；参数设置/读回一致；越界/NaN 返回 InvalidArgument；
  Destroy 后 Handle NotFound；native delete 计数正确。
- **E-T04**：Attach 成功；挂载中 Destroy Reverb = InvalidState；Detach 后销毁成功；
  backend 调用顺序 attach → detach → delete effect → delete slot。
- **E-T05**：Send index 0；send level 1.7 钳制为 1.0；内部 Send Filter GAINHF=1；
  Infinity 拒绝；错误 Slot Handle NotFound；Disconnect 清理 route 与 Filter。
- **E-T06**：Low-pass 参数设置/读回；非法 gain 拒绝；Direct Filter 绑定/解绑；
  绑定中 Destroy Filter = InvalidState；解绑后销毁成功。
- **E-T07**：非 Owner Thread `CreateReverb` 返回 invalid、WrongThread，后端调用 0。
- **E-T08**：Shutdown 顺序 disconnect aux → delete send filter → unbind direct →
  delete public filter → detach effect → delete effect → delete slot → source/buffer →
  context。
- **E-T09**：DeviceLost 后 Create/Attach/Destroy 返回 DeviceLost，后端调用不增加。
- **E-T10**：Effect 设置失败返回 BackendError；Engine 仍 Ready；capability 仍 Ok。
- **E-T11**：Source 析构的 Deferred Release 在 Owner Thread 关闭 Aux/Direct 关系；
  Buffer 仍有效并可显式销毁。
- **E-T14**：跨 Engine Handle/Source/Slot 被拒绝；Shutdown/ReInit 后旧 Handle 失效。

---

## 4. 真实设备 smoke

`OpenALRuntimeDeviceSmoke`（基础）与 `OpenALDeviceEfxSmoke`（EFX）均通过：

- 默认 OpenAL 设备 Init 成功；
- EFX capability = supported / routingSupported；
- 创建 Reverb、Aux Slot，Attach；
- 创建 Source、静音 Buffer，Connect Aux Send；
- 创建 Low-pass Filter，Bind Direct Filter；
- Play/Stop；
- Disconnect Aux Send、Unbind Direct Filter；
- 按依赖顺序销毁 Filter、Detach Effect、销毁 Reverb、销毁 Slot；
- Shutdown。

真实设备 EFX 未被 SKIP。该结果只代表本轮机器/Provider；不外推到所有 OpenAL Provider。

---

## 5. 回归与构建

| 项目 | Debug | Release | 证据 |
|---|---|---|---|
| `test_audio` | 145/145 | 145/145 | `build/t7b_test_audio_debug_run7.log` / `build/t7b_test_audio_release_run4.log` |
| `test_audio/test_bridge/test_hrc` CTest | 3/3 | 3/3 | `build/t7b_ctest_debug3.log` / `build/t7b_ctest_release3.log` |
| 全解决方案构建 | 0 error | 0 error | `build/t7b_full_debug_build3.log` / `build/t7b_full_release_build3.log` |

警告：最终 Debug/Release 全解决方案构建各出现 2 条既有 LNK4006（RHI 重复符号）
警告；未观察到新增编译警告。不宣称零警告/clean build。

---

## 6. 未验证 / 限制

- ASan/TSan NOT RUN；不宣称 sanitizer 或 race-free。
- 未覆盖所有 Provider、设备、EFX 参数读回差异或所有 aux send 上限组合。
- 未测试 Echo/Chorus、复杂效果链、Slot 共享/串联、Listener environment 映射。
- 未测试 CPU DSP 接入；未修改 Stack 1/Legacy Bypass。
- 未执行 Git 写入；`third_party/openal-soft` 未修改。
- 公共 EFX Handle 为非拥有值类型；未显式销毁的对象在 Engine Shutdown 清理，
  不提供 owning wrapper 析构语义（见 API 设计）。

---

## 7. 状态

| 项 | 状态 |
|---|---|
| E-T01～E-T14 | PASS（映射见 §2） |
| 基础音频 + T-7-A 回归 | PASS |
| 真实设备 EFX smoke | PASS（未 SKIP） |
| T-7-B 最终验收 | `PASS — SCOPE-BOUND / CLOSED` |

> 本记录已随 T-7-B 最终验收封存；正式裁决见 `docs/T-7-B-Closure-Record.md`。
