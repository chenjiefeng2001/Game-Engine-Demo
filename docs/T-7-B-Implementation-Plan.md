# T-7-B — OpenAL EFX Implementation Plan

**状态**：`GATE B — PASS — SCOPE-BOUND / CLOSED`
**日期**：2026-10-30
**上游契约**：`docs/T-7-Audio-EFX-Scope.md`（`SCOPE FROZEN`）、
`docs/T-7-Audio-GateA-Contract.md`（T-7-A `PASS — SCOPE-BOUND / CLOSED`）
**授权边界**：Stack 2 EFX 最小功能集；不覆盖 Git 提交、Stack 1、A0/A1、
CPU DSP 接入、Bridge/Editor ABI 或 `third_party/openal-soft` 修改。

---

## 1. B0 基线（2026-10-30）

| 项 | 观测 |
|---|---|
| 分支 | `avalonia` |
| HEAD | `74ad0999b35b1783ea9e58667966d3e84917dea8` |
| T-7-A | `PASS — SCOPE-BOUND / CLOSED`（实施记录 Final Acceptance） |
| T-7-B 范围 | `SCOPE FROZEN — PASS — SCOPE-BOUND / CLOSED` |
| `test_audio` | Debug/Release 129/129（112 既有 + 16 A-T + 1 设备 smoke） |
| 相关 CTest | `test_audio/test_bridge/test_hrc` 3/3 Debug/Release |
| `third_party/openal-soft` | `9fb7bb38cb35f50c692edaba47bad13d2bb879ba`，`D build/.empty`，未触碰 |
| 其他 dirty 子模块 | `third_party/d3d12ma`、`third_party/vma`，未触碰 |

工作树中已有 T-7-A 的音频改动、此前 UI-H/T-6 的 `editor_avalonia` 改动和既有未跟踪
文档/证据；本阶段不 reset/clean/覆盖这些文件。EFX 改动只进入 §4 白名单路径。

### 1.1 T-7-A 接口复核

| 模块 | 复核结果 |
|---|---|
| `AudioEngineRuntime`（`OpenALAudioState`） | Owner Thread、`phase`、`generation`、source/buffer registry、Deferred Release 均已具备，可扩展 EFX registry |
| `OpenALAudioEngine` | Init/Shutdown、状态转换、错误返回、资源注册与 Shutdown 顺序已实现 |
| `OpenALAudioSource` | `IsOwnedBy`、generation、native source identity、buffer binding 已实现 |
| `OpenALAudioBuffer` | buffer registry、generation、binding/retention 已实现 |
| `IOpenALBackend` | 已有 device/context/source/buffer/listener seam 与失败注入基础 |
| `OpenALRuntimeTest` | A-T01～A-T16 已有可复用 Fake 后端和调用记录 |

### 1.2 B0 结论

1. EFX 可由 Engine 统一拥有：在 `AudioEngineRuntime` 增加 EFX record registry，native ID
   仅保存在内部，公共 Handle 只含 engine identity/generation/serial/type。
2. EFX Handle 采用非拥有值类型；**不新增 owning wrapper 析构路径**，因此不会在析构中
   调用 AL。显式 `DestroyEfxObject` 或 Engine Shutdown 释放 native 资源；内部 Send Filter
   与 Source Route 由 Deferred Release/Shutdown 清理。该决定在 B1 冻结。
3. Source 销毁（含非 Owner 析构的 Deferred Release）必须先断开 Aux Send 与 Direct Filter，
   再删除 native source。
4. `Failed` 状态下 EFX registry 置为 inert、不得调用 AL；Re-Init 后旧 Handle 不得访问新 Context。
5. 跨 Engine 的 Source/Slot/Filter 必须通过 Engine identity + generation 拒绝。
6. T-7-A 的 A-T01～A-T16 全部保留，EFX 测试不得削弱或修改既有断言。

---

## 2. 实施顺序

`B1 API/参数冻结 → B2 EFX 能力检测 → B3 Reverb → B4 Auxiliary Effect Slot →
B5 Source Routing → B6 Low-pass Filter → B7 生命周期/Shutdown → B8 E-T01～E-T14 →
B9 Debug/Release 回归与封存`

不并行建立尚未确定的资源关系；B2～B6 每个切片都要求 owner/state/generation 检查与
后端调用计数断言。

---

## 3. 工作包与验收

| 工作包 | 内容 | 主要测试 |
|---|---|---|
| B0 | 基线与 T-7-A 接口复核 | 无代码 |
| B1 | API/参数/资源模型冻结 | 文档 |
| B2 | EFX capability detection | E-T01/E-T02/E-T13 |
| B3 | Reverb create/config/readback/destroy | E-T03 |
| B4 | Auxiliary Slot attach/detach/destroy | E-T04 |
| B5 | Source aux send route / direct filter | E-T05/E-T11 |
| B6 | Low-pass Filter | E-T06 |
| B7 | lifecycle / Shutdown / Failed / generation | E-T08/E-T09/E-T10/E-T14 |
| B8 | deterministic matrix + device smoke | E-T01～E-T14 |
| B9 | Debug/Release build, ctest, records | E-T12 + T-7-A regression |

---

## 4. 文件白名单

新增：

- `engine/include/Engine/Core/Audio/IAudioEfx.h`
- `docs/T-7-B-API-Design.md`
- `docs/T-7-B-Implementation-Record.md`（后续）
- `docs/T-7-B-Test-Record.md`（后续）
- `docs/T-7-B-Closure-Record.md`（后续）

修改：

- `engine/include/Engine/OpenAL/OpenALBackend.h`
- `engine/src/OpenAL/OpenALBackend.cpp`
- `engine/src/OpenAL/OpenALAudioState.h`
- `engine/include/Engine/OpenAL/OpenALAudioEngine.h`
- `engine/src/OpenAL/OpenALAudioEngine.cpp`
- `engine/src/OpenAL/OpenALAudioSource.cpp`（仅 EFX route cleanup 接入）
- `tests/test_audio/OpenALRuntimeTest.cpp`（仅扩展 Fake 后端；如 EFX 测试单独文件则
  `tests/CMakeLists.txt` 增加）
- `tests/CMakeLists.txt`（如新增 EFX 测试文件）

**不得修改**：

- `engine/src/Audio/AudioEngine.cpp`、`AudioSource.cpp`、`AudioClip.cpp`、
  `AudioSystem.cpp`、`AudioSourceComponent.cpp`、Stack 1 头文件
- `bridge/`、`editor_avalonia/`、`bridge/include/editor_bridge/capi.h`
- `third_party/openal-soft` 或任何子模块

---

## 5. 当前状态

| 阶段 | 状态 |
|---|---|
| T-7-A | `PASS — SCOPE-BOUND / CLOSED` |
| T-7-B Scope | `SCOPE FROZEN` |
| T-7-B Gate B | `IMPLEMENTATION AUTHORIZED` |
| B0 | `COMPLETE` |
| B1 | `API / PARAMETERS / RESOURCE MODEL FROZEN` |
| B2～B8 | `PASS — SCOPE-BOUND / CLOSED` |
| B9 | `PASS — SCOPE-BOUND / CLOSED`（最终验收 2026-10-30） |
| Git 写入 | 未授权 |

> 正式关闭裁决见 `docs/T-7-B-Closure-Record.md`；实施与证据见
> `docs/T-7-B-Implementation-Record.md` 与 `docs/T-7-B-Test-Record.md`。
