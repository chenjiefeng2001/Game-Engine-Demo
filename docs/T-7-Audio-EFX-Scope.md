# T-7-B — OpenAL EFX Scope & Acceptance Contract

**状态**：`SCOPE FROZEN — T-7-B PASS — SCOPE-BOUND / CLOSED`
**前置**：`T-7-A — PASS — SCOPE-BOUND / CLOSED`；
T-7-B 已实施，等待最终裁决；范围与验收矩阵仍按本文件冻结。
**日期**：2026-10-30
**范围**：Stack 2 的 OpenAL EFX 最小能力集：能力检测、Reverb、Auxiliary Effect Slot、
Source 到 Slot 的路由、Low-pass Filter。
**输入**：`docs/T-7-Audio-GateA-Contract.md`、D7-5、D7-6、`docs/T-7-Audio-Gate0-Scan.md`。

> 本文件冻结 T-7-B 的范围与验收矩阵，不授权源码实施，不新增 Bridge/Editor ABI。

---

## 0. Formal Scope Decisions

| 决策 | 内容 |
|---|---|
| **D7-5** | EFX 单独放入 T-7-B，不混入 T-7-A |
| **最小功能集** | EFX 能力检测、Reverb、Auxiliary Effect Slot、Source 到 Slot 路由、Low-pass Filter |
| **暂不纳入** | Echo、Chorus、其他扩展效果、复杂效果链、动态路由 |
| **可选后端能力** | 支持 EFX 的设备可创建/使用效果；不支持时返回明确 `Unsupported`；EFX 不可用不得导致基础音频 Init 失败 |
| **CPU DSP** | 现有 `AudioBusManager` / `LowPassFilter` / `FIRConvolution` 继续保留，但不在本阶段接入 OpenAL 播放链 |
| **线程与生命周期** | EFX 对象遵循 T-7-A 的 Owner Thread、Engine State、Resource Lifetime 与 Shutdown 规则，不得建立第二套线程模型 |
| **Bridge/Editor** | 不新增 Bridge ABI，不新增 Editor 音频接口；范围限制在 Engine Audio/Core、OpenAL 后端与测试 |

---

## 1. 范围

### 1.1 范围内

- EFX 扩展能力检测与缓存。
- Reverb Effect 的创建、配置、销毁。
- Auxiliary Effect Slot 的创建、挂载 Reverb、销毁。
- Source 到 Auxiliary Effect Slot 的路由建立/断开与 send level 钳制。
- Low-pass Filter 的创建、配置、绑定/解绑、销毁。
- EFX 对象在 Engine Ready/Failed/ShuttingDown/Shutdown 下的状态与生命周期。
- EFX 不可用/后端失败时的明确错误与降级。
- 确定性后端 seam 上的 EFX 测试，以及设备/EFX 可用性 SKIP 记录。
- 必要的内部诊断与测试入口；不新增 Bridge/Editor ABI。

### 1.2 范围外

- Echo、Chorus、Flanger、Distortion、Ring Modulator 等扩展效果。
- 多级效果链、任意 Slot 路由图、动态重路由、效果参数自动化/动画。
- 将 `AudioBusManager` 的 CPU DSP 接入 OpenAL 播放链，或建立第二套效果责任链。
- `Listener` 的 `EnvironmentPreset` / room 参数到 EFX 的映射；这些字段继续保留为未实现字段，
  除非另行授权。不得用它们假装 EFX 已接入。
- Stack 1 的 EFX 实现；Stack 1 冻结，不增加新功能。
- A0/A1 迁移、`AudioClip` ownership、`AudioSourceComponent`/`AudioSystem` bypass 收敛。
- Editor 音频 Inspector、播放预览、Bridge 音频 ABI。
- 自动设备恢复、专用音频线程、后台命令队列。
- `third_party/openal-soft` 的修改、更新、重置或清理。

---

## 2. 能力检测与降级（规范性）

1. **运行时查询**：EFX 能力必须在 Engine `Ready` 的 Owner Thread 上查询：
   - `alcIsExtensionPresent(device, "ALC_EXT_EFX")`
   查询结果缓存到 Engine 的 EFX capability 状态；不得仅根据 OpenAL Soft 版本推断支持。
   EFX 函数入口的可调用性以该设备扩展查询为准，不额外假设 `AL_EXT_EFX` 字符串存在。
2. **不支持时**：
   - EFX capability = `Unsupported`；
   - 基础音频 `Init` 仍可成功并进入 `Ready`；
   - 所有 EFX 创建/配置/路由调用返回 `Unsupported`，不执行 AL EFX 调用；
   - 不得把 `Unsupported` 伪装为 `Ok`，不得创建“空效果”对象。
3. **支持时**：
   - EFX 对象只在 Owner Thread、Engine `Ready` 状态创建/修改/销毁；
   - 创建失败返回 `nullptr`/明确失败，不得返回 native handle 0 的伪对象；
   - EFX 对象由 Engine 拥有并登记，wrapper 不得直接删除 native 对象。
4. **设备/能力变化**：EFX 能力按 Engine 生命周期缓存；`Shutdown` 后重新 `Init` 必须重新查询。
   设备丢失仍按 T-7-A 进入 `Failed`；EFX 不得独立恢复。

---

## 3. 最小信号路径（规范性）

T-7-B 只要求以下最小路径成立：

```
Source ──(direct filter)──> Low-pass Filter
   │
   └──(aux send, level [0,1])──> Auxiliary Effect Slot ──> Reverb Effect
```

- **Reverb** 必须绑定到 Auxiliary Effect Slot；
- **Low-pass Filter** 作为 Source 的 direct filter 绑定；
- **Source 到 Slot** 至少支持一个 send，send level 必须在 [0,1] 钳制；
- 不要求多 Slot 串联、Slot 到 Slot 路由或多效果叠加；
- 如果实现选择额外能力，不得改变上述最小路径的语义，也不得引入第二套效果 API。

---

## 4. 最小功能契约

### 4.1 Reverb Effect

- 必须能够创建至少一个 Reverb Effect。
- 必须支持至少一组可调整参数（例如 decay/room size/damping/wet 中的最小集合）；
  具体参数名与 API 形态由 Gate B 实现决定，但必须：
  - 可设置、可读回（通过后端 seam 或内部状态验证）；
  - 非法值被拒绝或钳制，并返回明确状态；
  - 默认配置不产生 AL 错误；
  - 可销毁，且销毁后句柄不可再用。
- 不要求特定预设列表；不要求 Echo/Chorus。

### 4.2 Auxiliary Effect Slot

- 必须能够创建至少一个 Auxiliary Effect Slot。
- 必须能够把 Reverb Effect 挂载到 Slot，并在销毁前解绑。
- Slot 必须由 Engine 拥有；Slot 数量上限若存在，必须在 Gate B 明确定义并在耗尽时返回
  明确失败（不得静默覆盖或返回伪句柄）。
- Slot 销毁必须在 Reverb/Filter 解绑之后、context 销毁之前完成。

### 4.3 Source → Slot 路由

- 必须支持将 Source 的 aux send 连接到 Slot，并可断开。
- send level 必须在 [0,1] 钳制；非法 source/slot 返回 `InvalidArgument`/`NotFound`。
- 同一 Source 的最小实现只需支持一个 send；若支持多个，必须有明确上限与失败语义。
- Source 销毁或 Engine Shutdown 必须先断开路由，再销毁 Slot/Reverb。

### 4.4 Low-pass Filter

- 必须能够创建至少一个 Low-pass Filter。
- 必须支持 cutoff（以及至少一个增益/直通参数；具体集合由 Gate B 冻结）。
- 必须能够绑定到 Source 作为 direct filter，并可解绑。
- 非法 cutoff/gain 必须拒绝或钳制并返回明确状态；不得产生 NaN/Inf 或 AL 错误。
- Filter 销毁必须在解绑之后、context 销毁之前。

### 4.5 不纳入的能力

- Echo/Chorus/其他扩展效果；
- 多级效果链、动态路由图、参数动画；
- Slot 之间的串联/并联图；
- 与 CPU DSP 总线的自动混合/合并；
- Listener environment/room 字段的 EFX 映射；
- Editor/Bridge 可见的 EFX 句柄或 ABI。

---

## 5. 错误语义（规范性）

T-7-B 复用 T-7-A 的 `AudioStatus` 语义，并补充：

| 场景 | 状态 | 规则 |
|---|---|---|
| EFX 扩展不可用 | `Unsupported` | 基础音频保持 `Ready`；不执行 EFX AL 调用 |
| EFX 对象创建失败 | `BackendError` 或 `DeviceLost` | 返回 `nullptr`/明确失败；不返回 native 0 对象 |
| 非 Owner Thread 调用 EFX | `WrongThread` | 进入 AL 前拒绝；后端调用计数为 0 |
| Engine 非 `Ready` | `NotInitialized` / `InvalidState` / `DeviceLost` / `ShuttingDown` | 按 T-7-A 状态矩阵拒绝 |
| 无效 source/slot/filter 句柄 | `NotFound` | 不得静默创建替身 |
| send level / cutoff 非法 | `InvalidArgument` 或按契约钳制 | 必须可观察，不得静默越界 |
| EFX 后端错误但 Context 仍有效 | `BackendError` | 基础 Engine 可保持 `Ready`；不得伪造效果成功 |
| 设备/Context 丢失 | `DeviceLost` | 按 T-7-A 进入 `Failed`；不自动恢复 |

**关键规则**：EFX `Unsupported` / `BackendError` 不得把基础音频 Engine 置为 `Failed`，
除非同时发生 T-7-A 定义不可恢复的 device/context 错误。EFX 失败不得影响已有基础播放路径。

---

## 6. 生命周期与 Shutdown（规范性）

EFX 对象遵循 T-7-A §5：

1. Engine 拥有 EFX/Filter/Slot 的 native 资源并登记；wrapper 不直接删除 native 对象。
2. 非 Owner Thread 的 wrapper 析构不得调用 AL；按 T-7-A 登记 Deferred Release。
3. `Shutdown` 在 `ShuttingDown` 状态下按以下顺序释放：
   1. 断开所有 Source → Slot 路由；
   2. 解除 Source direct filter 绑定；
   3. 销毁 Filters；
   4. 销毁 Effects；
   5. 销毁 Auxiliary Effect Slots；
   6. 继续 T-7-A 的 source/buffer/context/device 释放顺序。
4. `Failed` 状态下 Shutdown 采用尽力清理，不在无效 context 上做常规 AL 删除；
   EFX wrapper 置为 inert。
5. 外部 EFX wrapper 若存在，晚于 Engine 销毁时不得访问 engine/context；按 T-7-A
   `NotInitialized`/no-op 语义处理。
6. `Shutdown` 后再次 `Init` 必须重新查询 EFX capability；不得复用旧 capability 或旧对象。
7. EFX 对象绑定 Engine generation；Shutdown 后旧 EFX 对象永久 inert，再次 Init 后旧对象
   不得访问新 context/handle。

---

## 7. 测试矩阵（规范性）

测试优先使用 T-7-A 的确定性后端 seam；设备/EFX 能力不可用时记录 SKIP，不冒充 PASS。

| ID | 场景 | 通过条件 |
|---|---|---|
| E-T01 | EFX 能力检测（支持） | seam/设备报告支持；capability 缓存为 supported；无副作用 |
| E-T02 | EFX 能力检测（不支持） | capability = Unsupported；基础 Init 仍 Ready；EFX 调用返回 Unsupported 且无 EFX AL 调用 |
| E-T03 | Reverb 创建/配置/销毁 | 创建成功；至少一个参数设置/读回一致；销毁后句柄失效；无泄漏 |
| E-T04 | Aux Slot 生命周期 | 创建 Slot；挂载 Reverb；解绑；销毁；顺序正确；无 AL 错误 |
| E-T05 | Source → Slot 路由 | 连接成功；send level 钳制 [0,1]；断开成功；无效句柄返回明确错误 |
| E-T06 | Low-pass Filter | 创建/配置/绑定 Source/解绑/销毁；cutoff 非法值被拒绝或钳制；无 NaN/Inf |
| E-T07 | 非 Owner Thread EFX 调用 | 返回 WrongThread；后端调用计数为 0；状态不变 |
| E-T08 | EFX Shutdown 顺序 | 路由/Filter/Effect/Slot 在 context 销毁前释放；Shutdown 后无 AL 调用 |
| E-T09 | 设备丢失 | 基础 Engine 进入 Failed；EFX 调用返回 DeviceLost；不自动恢复 |
| E-T10 | EFX BackendError | 返回 BackendError；基础 Engine 仍 Ready（若 context 有效）；不伪造成功 |
| E-T11 | Source/Buffer 生命周期 | 路由与效果存在时 source 持有 buffer；销毁顺序不产生失效 buffer 访问 |
| E-T12 | 基础音频回归 | T-7-A A-T01～A-T15、现有 `test_audio` 112 用例保持通过 |
| E-T13 | EFX 不可用不阻塞基础播放 | 无 EFX 设备上 Init/Play/Stop/Shutdown 正常；EFX 返回 Unsupported |
| E-T14 | EFX 对象跨 Engine 生命周期 | Shutdown 后旧 EFX 对象 inert；再次 Init 后旧对象不得访问新 context/handle |

### 7.1 断言纪律

- 每个 EFX 调用失败用例必须断言状态与后端调用次数/顺序；只检查返回值不够。
- 设备/EFX SKIP 必须记录原因与环境；不得将 SKIP 计入 PASS。
- 真实设备 EFX 支持可能因机器而异；seam 测试覆盖状态与错误路径，真实设备测试覆盖
  最小可听/可查询路径。
- 首次失败保留；不得重跑到绿为止。

---

## 8. 验收条件

T-7-B 只有在以下条件满足时才可申请验收：

- T-7-A 已验收通过，且 T-7-A 契约未被回退。
- E-T01～E-T14 在确定性 seam 与可用设备组合下通过，或明确 SKIP 并记录原因。
- 基础音频回归（T-7-A 矩阵、112 个 `test_audio` 用例）保持通过。
- EFX 对象在 Owner Thread 和 Engine 状态下创建/销毁；无 AL 在 context 销毁后调用。
- 不支持 EFX 的设备上基础音频仍可用；EFX 返回 `Unsupported`。
- 未实现 Echo/Chorus/复杂链；未接入 CPU DSP 总线；未新增 Bridge/Editor ABI。
- 未修改 Stack 1、`AudioClip` ownership、A0/A1 或 `third_party/openal-soft`。
- Debug/Release 分开记录；0 error；warnings 按实际观测记录，不宣称零警告/clean build。
- 验收报告记录实际命令、测试名、设备/EFX 可用性、SKIP 原因与证据路径。

---

## 9. 阶段状态

| 阶段 | 状态 |
|---|---|
| T-7-A Runtime Safety | `PASS — SCOPE-BOUND / CLOSED` |
| T-7-B EFX | `PASS — SCOPE-BOUND / CLOSED` |
| T-7-B 实现授权 | `AUTHORIZED — GATE B COMPLETE` |
| Bridge ABI | `NOT AUTHORIZED` |
| Editor 音频集成 | `NOT AUTHORIZED` |

> 本文件冻结的是 EFX 的范围与验收边界，不是实现方案；具体 API、参数名、对象上限与
> 内部数据结构在 T-7-B Gate B 中冻结，且不得改变本文件的最小功能集与错误语义。
