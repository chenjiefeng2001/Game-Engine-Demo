# T-7-A — Audio Runtime Safety & Lifecycle Gate A Contract

**状态**：`FROZEN`
**裁决**：`T-7 GATE 0 — PASS — READ-ONLY COMPLETE`（2026-10-30）；
`D7-1`～`D7-6` 已批准并作为本契约的正式输入。
**日期**：2026-10-30
**范围**：Stack 2（`IAudioEngine` / `OpenALAudioEngine` / `IAudioSource` / `IAudioBuffer`）
的运行时不变量：线程归属、Engine 状态、资源生命周期、失败处理、设备故障与关闭。
**实施状态**：`T-7-A — PASS — SCOPE-BOUND / CLOSED`
（Gate B 已验收，2026-10-30；证据见 `docs/T-7-A-Implementation-Record.md`）。
本契约条款未改变；T-7-B EFX 独立延期。

**输入**：

- `docs/T-7-Audio-Stage0-Baseline.md`
- `docs/T-7-Audio-Gate0-Scan.md`
- `docs/ADR-Audio-Canonical-Stack.md`（Stack 2 canonical，Accepted 2026-10-04）
- `docs/Audio-Migration-Design-Freeze.md`（A0/A1 状态，仅供边界参考）

---

## 0. Formal Adjudication Record

| 决策 | 内容 | 本契约约束 |
|---|---|---|
| **D7-1** | Stack 2 为唯一后续开发主线；Stack 1 冻结，不增加新功能；Stack 1 调用方暂时保留；Stack 1 退休另行裁决 | 本契约只覆盖 Stack 2；不得在 Stack 1 实现 EFX、Buffer Ownership 或线程安全机制 |
| **D7-2** | 单 Owner Thread；不建立专用音频线程；所有 AL/ALC 后端操作在 Owner Thread；非 Owner Thread 在进入 OpenAL 前拒绝并返回明确错误；`Shutdown()` 在 Owner Thread | §4、§5、§6、§7 的线程与资源规则 |
| **D7-3** | 不恢复 A0/A1，不重启原音频迁移；T-7-A 只修 Stack 2 运行时不变量 | §2 的非目标；不触碰 `AudioClip` ownership、Application/Editor 音频路径、Stack 1 迁移 |
| **D7-4** | 设备故障 Fail-Closed，不自动恢复；状态为 `Uninitialized/Ready/Failed/ShuttingDown/Shutdown` | §3 的状态机；§7 的设备失败规则 |
| **D7-5** | EFX 单独放入 T-7-B；最小集为能力检测、Reverb、Aux Effect Slot、Source 路由、Low-pass；Echo/Chorus、复杂链暂不纳入 | EFX 不在本契约实现；由 `docs/T-7-Audio-EFX-Scope.md` 冻结范围 |
| **D7-6** | 不新增 Bridge ABI，不新增 Editor 音频接口；T-7-A/T-7-B 限制在 Engine Audio/Core、OpenAL 后端及测试 | §2 非目标；`bridge/include/editor_bridge/capi.h` 不得改动 |

### 术语：Thread Confinement

本契约使用 **Thread Confinement**（线程归属约束），不是一般意义上的 Thread Safety。
它不声明音频 API 可以被任意线程并发调用；它要求所有 Stack 2 后端操作被限制在唯一的
Owner Thread，并在违反时明确拒绝。任何实现或验收不得把“增加了线程检查”表述为
“整个音频系统已支持并发访问”。

---

## 1. 术语与范围

### 1.1 术语

| 术语 | 定义 |
|---|---|
| Stack 2 | `IAudioEngine`、`IAudioSource`、`IAudioBuffer` 抽象及 `OpenALAudioEngine`、`OpenALAudioSource`、`OpenALAudioBuffer` 实现 |
| Owner Thread | 第一次调用 `Init()` 的线程；该线程负责本 engine 生命周期的所有后端操作与 `Shutdown()` |
| Backend Operation | 任何 `al*` / `alc*` 调用，包括资源创建、播放、参数修改、查询、删除、错误读取 |
| Native Resource | OpenAL 的 device/context/source/buffer 等后端对象 |
| Resource Wrapper | 通过 `shared_ptr<IAudioSource>` / `shared_ptr<IAudioBuffer>` 暴露给调用方的 C++ 对象 |
| Deferred Release | 非 Owner Thread 上发生 wrapper 析构时，不调用 AL，只登记待释放记录的机制 |
| Legacy Bypass | `AudioSystem`、`AudioSourceComponent`、`AudioClip` 以及 Stack 1 中直接调用 `al*` 的路径 |
| Failed | 初始化失败、设备丢失或不可恢复后端错误后的 fail-closed 状态 |

### 1.2 范围内

- `OpenALAudioEngine` 的 device/context 生命周期、状态机、Owner Thread 检查、错误返回。
- `OpenALAudioSource` / `OpenALAudioBuffer` 的 wrapper 状态、资源注册、延迟释放、析构语义。
- `IAudioEngine` / `IAudioSource` / `IAudioBuffer` 的内部 API 语义调整（不含 Bridge ABI）。
- 确定性后端测试 seam 与 T-7-A 测试矩阵。
- 必要的内部日志与诊断；不得引入 Editor/Bridge 可见接口。

### 1.3 范围外（本阶段不做）

- EFX、Reverb、Aux Effect Slot、Filter、Source 路由（T-7-B）。
- 恢复 A0 provisioning 或 A1 ownership migration。
- `AudioClip` 的 `m_BufferID` ownership 改造、`AudioSourceComponent` 注入 engine、
  `AudioSystem` bypass 收敛。
- Stack 1 的删除、迁移或新功能；Stack 1 现有调用方保留。
- Application/Editor/Bridge 音频生产路径、音频 Inspector、播放预览、新 Bridge ABI。
- 专用音频线程、后台命令队列、自动设备重连、播放状态恢复。
- `third_party/openal-soft` 的修改、更新、重置或清理。
- 任何源码实施；本文件只冻结契约。

---

## 2. 非目标与兼容边界

- **不删除 Stack 1**：`Engine::Audio::AudioEngine` / `AudioSource` 保持现状；不因 T-7-A
  改变其行为或增加功能。
- **不修复 Legacy Bypass**：`AudioSystem`、`AudioSourceComponent`、`AudioClip` 的裸
  `alSourcei` / `alSourcePlay` / `alGenBuffers` 路径不在 T-7-A 的强制边界内；它们仍须
  遵守“只在 Owner Thread 使用”的全局规则，但本阶段不为其增加新机制或迁移。
- **不改变 `AudioClip` 契约**：`AudioClip::GetBufferHandle()`、`m_BufferID`、`Release()`
  语义保持；A1 仍冻结。
- **不新增 ABI**：`capi.h`、`EditorHost`、`editor_avalonia` 不增加音频导出/调用。
- **不引入通用并发能力**：只实现 Thread Confinement；不承诺多线程同时调用音频 API。
- **不自动恢复**：Failed 后只能显式 Shutdown → Init；不重建 source/buffer/播放位置。

---

## 3. Engine 状态机（规范性）

### 3.1 状态定义

| 状态 | 含义 | 后端操作 | 允许的入口 |
|---|---|---|---|
| `Uninitialized` | 构造后尚未 Init；无 device/context/owner | 禁止 | `Init`；`Shutdown` 为无操作成功 |
| `Ready` | device/context 有效；Owner Thread 已绑定 | 仅 Owner Thread 允许 | `Init`（同线程幂等）、`Shutdown`、全部 Stack 2 操作 |
| `Failed` | Init 失败、设备丢失或不可恢复后端错误；fail-closed | **禁止** | Owner Thread 的 `Shutdown`；`Init` 被拒绝 |
| `ShuttingDown` | 正在释放资源；瞬时状态 | **禁止新操作** | 仅当前 Owner Thread 的关闭流程 |
| `Shutdown` | context/device 已释放；无后端资源 | 禁止 | `Init`（可绑定新 Owner）；Owner Thread 的重复 `Shutdown` 为无操作 |

### 3.2 状态转换

| 当前状态 | 事件 | 结果 | 备注 |
|---|---|---|---|
| `Uninitialized` | `Init()` 成功 | `Ready` | 调用线程成为 Owner Thread |
| `Uninitialized` | `Init()` 失败 | `Failed` | 调用线程仍绑定为清理 Owner；不得留下 device/context |
| `Uninitialized` | `Shutdown()` | `Uninitialized` | 无资源，无操作成功；不要求 Owner |
| `Ready` | 同 Owner `Init()` | `Ready` | 幂等；不得创建第二个 device/context，不得产生后端调用 |
| `Ready` | 非 Owner `Init()` | `Ready` | 返回 `WrongThread`；状态不变 |
| `Ready` | Owner `Shutdown()` | `ShuttingDown` → `Shutdown` | 释放顺序见 §5.4 |
| `Ready` | 非 Owner `Shutdown()` | `Ready` | 返回 `WrongThread`；不得触碰后端 |
| `Ready` | Owner 检测到设备丢失/不可恢复错误 | `Failed` | 停止后端；见 §7 |
| `Failed` | 同 Owner `Init()` | `Failed` | 返回 `InvalidState`；必须先 Shutdown |
| `Failed` | 非 Owner `Init()` | `Failed` | 返回 `WrongThread`；状态不变 |
| `Failed` | Owner `Shutdown()` | `ShuttingDown` → `Shutdown` | 尽力清理；不得在无效 context 上调用普通 AL 删除 |
| `Failed` | 非 Owner `Shutdown()` | `Failed` | 返回 `WrongThread` |
| `ShuttingDown` | 任何新的 `Init`/`Shutdown`/资源操作 | `ShuttingDown` | 返回 `ShuttingDown`；不得重入 |
| `Shutdown` | 任意线程 `Init()` 成功 | `Ready` | 调用线程成为新的 Owner Thread |
| `Shutdown` | 任意线程 `Init()` 失败 | `Failed` | 调用线程成为清理 Owner |
| `Shutdown` | Owner `Shutdown()` | `Shutdown` | 幂等；无后端调用 |
| `Shutdown` | 非 Owner `Shutdown()` | `Shutdown` | 返回 `WrongThread`；无后端调用 |

**Owner 绑定澄清**：为使失败清理也有确定归属，Owner Thread 在第一次 `Init()` 调用时绑定；
`Init()` 成功时该线程即 D7-2 的 Owner Thread；`Init()` 失败时该线程仍负责 `Failed` 状态的
`Shutdown()` 清理。`Shutdown` 完成后再次 `Init()` 可以绑定新的 Owner Thread。
该澄清不改变 D7-2 的“成功 Init 线程成为 Owner”方向。

### 3.3 操作可用性矩阵

| 操作 | `Uninitialized` | `Ready` | `Failed` | `ShuttingDown` | `Shutdown` |
|---|---|---|---|---|---|
| `Init` | 允许 | 同 Owner 幂等；非 Owner 拒绝 | Owner `InvalidState`；非 Owner `WrongThread` | 拒绝 `ShuttingDown` | 允许 |
| `Shutdown` | 无操作成功 | Owner 允许；非 Owner 拒绝 | Owner 允许；非 Owner 拒绝 | 拒绝 | Owner 幂等；非 Owner 拒绝 |
| `CreateSource` / `CreateBuffer` | `NotInitialized` | Owner 允许 | Owner `DeviceLost`；非 Owner `WrongThread` | `ShuttingDown` | `NotInitialized` |
| `DestroySource` / `DestroyBuffer` | `NotInitialized` | Owner 允许 | Owner `DeviceLost`；非 Owner `WrongThread` | `ShuttingDown` | `NotInitialized` |
| Source 播放/暂停/停止/参数 | `NotInitialized` | Owner 允许 | Owner `DeviceLost`；非 Owner `WrongThread` | `ShuttingDown` | `NotInitialized` |
| Source/Buffer 查询 | `NotInitialized` | Owner 允许 | Owner `DeviceLost`；非 Owner `WrongThread` | `ShuttingDown` | `NotInitialized` |
| Listener / Master / DistanceModel | `NotInitialized` | Owner 允许 | Owner `DeviceLost`；非 Owner `WrongThread` | `ShuttingDown` | `NotInitialized` |
| `Update` | `NotInitialized` | Owner 允许 | Owner `DeviceLost`；不再触碰 AL | `ShuttingDown` | `NotInitialized` |
| `GetState` | 任意线程只读 | 任意线程只读 | 任意线程只读 | 任意线程只读 | 任意线程只读 |

`GetState` 必须是无 AL、线程安全的状态读取；其实现可使用原子状态或等价同步。
在 `Ready` / `Failed`（Owner 已绑定）中，线程检查先于状态/资源检查：非 Owner 一律
`WrongThread`，Owner 才按上表得到状态/资源错误。除 `GetState` 与 `Uninitialized` 的
`Shutdown` 外，所有入口都按上表拒绝，并返回明确状态。

---

## 4. Owner Thread 与 Thread Confinement（规范性）

1. **绑定**：Owner Thread 按 §3.2 的澄清绑定；在 `Ready`/`Failed` 期间不得更换。
2. **检查点**：所有 Stack 2 公共操作必须在进入任何 `al*`/`alc*` 调用之前检查
   “当前线程 == Owner Thread”。
3. **拒绝语义**：非 Owner Thread 调用必须返回 `WrongThread`；不得静默忽略、不得降级为
   no-op、不得先执行部分后端操作。对返回 `void` 的现有接口，实施时必须引入可观察的
   `AudioStatus`（或等价结果）返回；不能保持“无返回值且静默失败”。
4. **Shutdown**：`Shutdown()` 只允许 Owner Thread；`Shutdown` 全过程（含资源删除、
   context 销毁、device 关闭）在 Owner Thread 完成。
5. **Update**：`OpenALAudioEngine::Update()` 只允许 Owner Thread；设备健康检查、延迟释放
   队列排空、状态转换都在此调用内完成。
6. **无专用线程/队列**：不建立后台音频线程、不建立新的跨线程命令队列；本契约不把
   “将来可能加线程”作为实现前提。
7. **析构例外**：非 Owner Thread 上的 Resource Wrapper 析构不得调用任何 AL/ALC；
   只能登记 Deferred Release（§5.3）。这是线程约束下的唯一跨线程登记操作。
8. **Debug 诊断**：Debug 构建可以在拒绝点记录警告/断言；Release 行为仍必须返回
   `WrongThread` 并保持无后端调用。不能只靠断言实现约束。
9. **术语纪律**：验收报告只能写“Stack 2 已实现 Thread Confinement”，不得写
   “音频系统线程安全”或“支持并发调用”。

---

## 5. Resource Lifetime（规范性）

### 5.1 所有权

- **Engine 拥有 Native Resource**：`OpenALAudioEngine` 创建并登记其 source/buffer；
  wrapper 只持有句柄/状态/引擎生命周期令牌，不得在 engine 不知情时直接删除 native 对象。
- **Buffer retention**：`IAudioSource::Play(shared_ptr<IAudioBuffer>)` 期间 source 必须持有
  buffer 的 `shared_ptr`；`Stop()`/重新绑定前不得释放。native buffer 只有在没有 source
  绑定且 wrapper 生命周期结束后才可删除。
- **创建失败**：`CreateSource`/`CreateBuffer` 失败必须返回 `nullptr`（或等价明确失败），
  不得返回 `m_SourceID == 0` / `m_BufferID == 0` 的伪有效 wrapper；失败前已创建的
  native 资源必须在 context 有效时清理。
- **不改变 Legacy**：`AudioClip` 的 `m_BufferID` ownership 不变；T-7-A 不把 `AudioClip`
  纳入 Stack 2 resource registry。
- **Generation/epoch**：每次成功 `Init` 产生新的 monotonically increasing generation；
  Resource Wrapper 绑定创建时的 generation。`Shutdown` 后旧 generation 永久失效，
  即使之后再次 `Init` 成功，旧 wrapper 也必须保持 inert，方法返回 `NotInitialized`，
  不得把旧 native handle 解释为新 context 的资源。

### 5.2 Owner Thread 上的销毁

- Engine `Ready` 且当前是 Owner Thread：`DestroySource`/`DestroyBuffer` 立即从 registry
  移除，并在 context 有效时删除 native 对象；对应 wrapper 变为 inert。
- 重复销毁同一资源必须是明确的 `NotFound`（或等价 status），不得二次调用 AL 删除。
- Source 销毁前必须停止播放并解绑 buffer；buffer 删除必须在所有绑定 source 解绑之后。

### 5.3 非 Owner Thread / 晚于 Engine 的销毁

- **非 Owner Thread 析构**：wrapper 析构不得调用 AL；应登记 Deferred Release 记录，
  由 Owner Thread 在下一次 `Update()` 或 `Shutdown()` 中处理。该登记机制必须线程安全，
  且不得执行 AL。
- **Engine 已 `Failed`/`ShuttingDown`/`Shutdown`**：wrapper 析构不得调用 AL；若 native
  资源仍由 engine 登记，由 engine 的 `Shutdown` 统一处理；若已释放，wrapper 直接 inert。
- **外部 wrapper 晚于 Engine 生命周期**：允许 `shared_ptr<IAudioSource/IAudioBuffer>` 晚于
  engine 对象析构；其析构为 no-op，方法返回 `NotInitialized`，不得访问已销毁的 engine 或
  context，必须通过共享状态/generation 检查实现。不得依赖“engine 一定晚于 wrapper 析构”。
- **Deferred Release 不得无限累积**：Owner Thread 必须在 `Update()` 中排空；`Shutdown()`
  必须最终排空或在 context 销毁前统一处理。

### 5.4 Shutdown 顺序（规范性）

Owner Thread 执行 `Shutdown()` 时：

1. 将状态置为 `ShuttingDown`；此后拒绝所有新操作；
2. 停止所有 source；解绑/释放 source 持有的 buffer 引用；
3. 删除所有仍登记的 source；
4. 删除所有仍登记的 buffer；
5. 在 context 销毁前排空 Deferred Release；仍有效的 native 资源按 source → buffer 删除，
   无法删除的标记 inert；
6. `alcMakeContextCurrent(nullptr)`；
7. `alcDestroyContext`；
8. `alcCloseDevice`；
9. 状态置为 `Shutdown`；清空 registry 与 deferred queue。

`Failed` 状态的 Shutdown 允许跳过“普通 AL 删除”，直接释放 context/device，但必须把
wrapper 置为 inert；不得在无效 context 上调用普通 `alDelete*`。`ShuttingDown` 期间
不得重入 `Init`/`Shutdown`/资源操作。

### 5.5 源码级高风险路径

Gate 0 的 `AudioTestApp` 路径（`AudioTestApp` 析构 → `OpenALAudioEngine::Shutdown()` →
外部 `m_Source` 析构 → `alDeleteSources`）必须由 §5.3 的“晚于 Engine 的 wrapper 析构”
规则覆盖。验收必须证明：Engine 已 Shutdown 后销毁外部 source wrapper 不产生 AL 调用，
而不是仅删除析构函数中的某一行 AL 调用。

---

## 6. 错误语义（规范性）

### 6.1 `AudioStatus`

T-7-A 必须引入统一的、可观察的错误结果（名称可按实现调整，语义必须一致）：

| 状态 | 含义 | 典型来源 |
|---|---|---|
| `Ok` | 成功/已接受 | 正常调用 |
| `NotInitialized` | Engine 未 Init 或已 Shutdown | 未初始化/已关闭后的资源调用 |
| `WrongThread` | 调用线程不是 Owner Thread | 所有 Stack 2 入口 |
| `InvalidState` | 当前状态不允许该操作 | `Failed` 下 Init、`ShuttingDown` 下新操作 |
| `ShuttingDown` | Engine 正在关闭 | `ShuttingDown` 期间 |
| `DeviceLost` | 设备丢失/不可恢复后端错误 | Failed 状态或后端错误 |
| `BackendError` | 可识别的 AL/ALC 失败 | 创建/绑定/删除失败 |
| `InvalidArgument` | 参数非法 | 空 buffer、无效索引/范围 |
| `NotFound` | 资源句柄不存在/已释放 | 重复销毁 |
| `Unsupported` | 可选能力不可用 | T-7-B EFX 能力检测 |

### 6.2 规则

- **Init 失败**：返回失败并进入 `Failed`；不得留下“看起来 Ready”的 engine。
  `Failed` 下再次 `Init` 返回 `InvalidState`；必须显式 `Shutdown` 后再 `Init`。
- **Create 失败**：返回 `nullptr`/明确失败，不返回 native ID 0 的伪对象；失败原因可由
  `AudioStatus` 观察。不得用异常跨越内部 C++ 边界（T-7-A 不涉及 Bridge ABI）。
- **非 Owner Thread**：在进入任何后端调用前返回 `WrongThread`；状态不变；不得记录为
  成功，也不得部分执行。
- **设备丢失**：进入 `Failed`；后续 Owner Thread 资源操作返回 `DeviceLost`，`Init` 返回
  `InvalidState`；`Update()` 不再访问 AL；不自动重连、不恢复 source/buffer/播放状态。
- **EFX 不可用**（T-7-B）：返回 `Unsupported`，不得使基础音频 `Init` 失败或把 engine
  置为 `Failed`；EFX 错误不得伪装为 `Ok`。
- **重复 Shutdown**：`Shutdown` 状态下的重复 Shutdown 返回成功且无后端调用；不得重复
  销毁 context/device。
- **状态查询**：`GetState` 无 AL，可任意线程读取；不得因查询而改变状态。
- **日志**：错误应可诊断；日志不能替代返回状态，也不能在非 Owner Thread 上调用 AL。

---

## 7. 设备故障（规范性）

- **初始化失败**：`alcOpenDevice` / `alcCreateContext` / `alcMakeContextCurrent` 任一失败
  必须清理已创建部分并进入 `Failed`；不得留下 device/context 或伪 Ready 状态。
- **设备丢失检测**：
  - 若 `ALC_EXT_disconnect` 可用，Owner Thread 的 `Update()` 必须查询 `ALC_CONNECTED`；
    断开时进入 `Failed`。
  - 若无该扩展，任何核心 AL/ALC 调用返回不可恢复错误（如无效 device/context）时进入
    `Failed`。
  - 检测到 `Failed` 后立即禁止后续后端调用；`Update()` 返回 `DeviceLost`。
- **恢复**：不自动恢复；必须显式 `Shutdown` → `Init`。新 Init 可绑定新 Owner Thread。
- **失败清理**：`Failed` 下 `Shutdown` 尽力释放 context/device，不保证能够删除已失效的
  native source/buffer；不得在无效 context 上做常规 AL 删除，不得崩溃。
- **测试**：设备断开/不可恢复错误必须通过可注入后端 seam 测试；不能只靠真实设备。

---

## 8. 测试基础设施与矩阵（规范性）

### 8.1 测试基础设施

**主路径：确定性后端 seam**

- 在 `OpenALAudioEngine` 与真实 AL/ALC 之间引入一个内部后端 seam（接口/函数表均可），
  生产实现调用真实 OpenAL；测试实现记录调用、注入失败、模拟 `ALC_CONNECTED`。
- seam 只服务 EngineCore 与 `test_audio`，不进入 Bridge ABI，不暴露给 Editor。
- 所有线程约束测试必须断言：被拒绝的操作产生 **0 次后端调用**。
- 所有生命周期测试必须能在无音频设备环境中确定性运行。

**辅助：设备后端冒烟**

- 在可用默认设备上执行 Init/Create/Play/Stop/Shutdown 冒烟；不可用/失败时记录
  `SKIP` 与原因，不得记为 PASS。
- 设备丢失/断开若无法安全自动化，使用 seam 测试其状态转换；真实设备手动探针单独记录。
- 不把 OpenAL Soft null/loopback 驱动作为验收前提；可在 Gate B 评估，但不得依赖。

**回归**

- 现有 `test_audio` 的 112 个 headless 用例必须保持通过；不得削弱断言或把 SKIP 当 PASS。
- Debug/Release 分开记录；不宣称零警告/clean build。

### 8.2 T-7-A 测试矩阵

| ID | 场景 | 通过条件 |
|---|---|---|
| A-T01 | 正常 Init | Owner Thread 正确记录；状态 `Ready`；后端调用序列完整且无重复 device/context |
| A-T02 | 重复 Init | 不产生重复 Device/Context；同 Owner 幂等成功；无额外后端初始化调用 |
| A-T03 | 错误线程调用 | 返回 `WrongThread`；后端调用计数为 0；状态不变 |
| A-T04 | 错误线程资源释放 | 不在错误线程触发 AL；Deferred Release 被登记；Owner 后续处理时删除 |
| A-T05 | Engine Shutdown | source/buffer/context/device 按 §5.4 顺序释放；无资源在 context 销毁后删除 |
| A-T06 | 外部 Source 晚于 Engine 销毁 | 不访问已销毁 context；后端调用计数不增加；无崩溃 |
| A-T07 | Buffer 创建失败 | 返回明确失败；不暴露 native ID 0 的伪对象；已创建部分被清理 |
| A-T08 | Source 创建失败 | 同上 |
| A-T09 | Device Disconnect | seam 报告断开/不可恢复错误；状态进入 `Failed`；后续不再调用后端 |
| A-T10 | Failed 后继续播放 | 返回 `DeviceLost`；后端调用计数为 0 |
| A-T11 | 重复 Shutdown | 不重复释放资源；无后端调用；状态保持 `Shutdown` |
| A-T12 | Init 失败后再次 Init | 第一次失败进入 `Failed`；再次 Init 被拒绝；Shutdown 后 Init 可成功并绑定新 Owner |
| A-T13 | Buffer retention | source 播放期间持有 buffer；buffer wrapper 释放不导致 native buffer 提前删除；Stop 后按序释放 |
| A-T14 | wrapper 晚于 engine 对象 | 外部 source/buffer wrapper 析构不访问已销毁 engine/context；方法返回 `NotInitialized`，析构无操作 |
| A-T15 | wrapper 跨 Engine 生命周期 | Shutdown 后旧 wrapper 永久 inert；再次 Init 后旧 wrapper 不得访问新 context/handle；返回 `NotInitialized` |

### 8.3 断言纪律

- 每个失败用例必须同时断言“返回状态”和“后端调用次数/顺序”；只断言返回值不够。
- 线程测试不得通过制造数据竞争来“证明安全”；应断言非 Owner 调用被拒绝且没有 AL。
- 设备相关 SKIP 必须记录环境与原因；不得把“没有设备”解释为通过。
- 首次失败保留；不得重跑到绿为止。

---

## 9. T-7-A 工作包与验收

| 工作包 | 内容 | 主要测试 |
|---|---|---|
| A1 | Owner Thread 绑定与检查 | A-T01、A-T03、A-T04 |
| A2 | Engine 状态机与状态查询 | A-T01、A-T02、A-T09、A-T10、A-T12 |
| A3 | Resource registry、retention、Deferred Release | A-T04、A-T05、A-T06、A-T13、A-T14、A-T15 |
| A4 | 创建/销毁失败与伪有效对象禁止 | A-T07、A-T08、A-T12 |
| A5 | 设备故障 fail-closed | A-T09、A-T10 |
| A6 | Shutdown 顺序与幂等 | A-T05、A-T06、A-T11 |

### 9.1 T-7-A 验收条件

- A-T01～A-T15 在确定性后端 seam 下 Debug/Release 均通过。
- 真实设备冒烟通过，或明确记录 SKIP；不得用 SKIP 冒充 PASS。
- 现有 `test_audio` 112 用例及既有回归不被削弱。
- 所有非 Owner 调用、`Failed` 下的资源/播放调用、`ShuttingDown` 下的新调用、`Shutdown`
  下的资源调用产生 0 次后端调用；`Shutdown` 自身的清理流程除外。
- 所有资源在 context 有效时删除；晚于 engine 的 wrapper 析构不产生 AL。
- 创建失败不返回 native ID 0 的伪对象；Init 失败不留下伪 Ready。
- 未修改 `capi.h`、未新增 Bridge/Editor ABI、未实现 EFX、未恢复 A0/A1、未删除 Stack 1。
- Debug/Release 构建 0 error；warnings 按实际观测记录，不宣称零警告或 clean build。
- 验收报告必须记录实际命令、测试名、环境、设备可用性、失败/SKIP 原因与证据路径。

### 9.2 预期实施表面（非授权）

T-7-A 的 Gate B 预期只涉及以下内部文件；最终清单在 Gate B 确认，不得顺手扩展：

- `engine/include/Engine/Core/Audio/IAudioEngine.h`
- `engine/include/Engine/Core/Audio/IAudioSource.h`
- `engine/include/Engine/Core/Audio/IAudioBuffer.h`
- `engine/include/Engine/OpenAL/OpenALAudioEngine.h`
- `engine/include/Engine/OpenAL/OpenALAudioSource.h`
- `engine/include/Engine/OpenAL/OpenALAudioBuffer.h`
- `engine/src/OpenAL/OpenALAudioEngine.cpp`
- `engine/src/OpenAL/OpenALAudioSource.cpp`
- `engine/src/OpenAL/OpenALAudioBuffer.cpp`
- 新增内部 backend seam 文件（若采用）
- `tests/test_audio/` 与 `tests/CMakeLists.txt`（T-7-A 测试）

**不得**修改：`engine/src/Audio/AudioEngine.cpp`、`AudioSource.cpp`、`AudioClip.cpp`、
`AudioSystem.cpp`、`AudioSourceComponent.cpp`、Stack 1 头文件、`capi.h`、Editor/Bridge。

---

## 10. 边界与残余风险

### 10.1 本契约的保护边界

- `third_party/openal-soft` 不修改、不更新、不重置、不清理。
- 不删除 Stack 1；不恢复 A0/A1；不迁移 `AudioClip` ownership。
- 不建立专用音频线程或跨线程命令队列。
- 不新增 Bridge/Editor ABI；不接编辑器音频面板。
- 不执行 commit、push、tag、merge、reset、clean 或 stash。
- 本契约不授权任何源码修改；T-7-A 实施需 Gate B 单独授权。

### 10.2 残余风险（T-7-A 不解决，必须记录）

- **Legacy Bypass**：`AudioSystem`、`AudioSourceComponent`、`AudioClip`、Stack 1 的裸 AL
  路径不在 T-7-A 强制边界内；它们仍可能跨线程/跨 shutdown 使用，未来需 A1 或独立迁移。
- **Stack 1**：`Engine::Audio::AudioEngine`/`AudioSource` 冻结但不安全；T-7-A 不修复。
- **EFX**：T-7-B 前不存在 EFX；不能把基础音频安全验收表述为 EFX 已完成。
- **自动恢复**：设备丢失后不自动恢复；必须显式 Shutdown/Init。
- **通用并发**：只保证 Thread Confinement；不保证任意多线程调用安全。
- **设备测试**：真实设备可用性、EFX 支持、设备断开注入可能 SKIP；必须以 seam 证据补足。

---

## 11. 阶段状态

| 阶段 | 状态 |
|---|---|
| T-7 Gate 0 | `PASS — READ-ONLY COMPLETE` |
| T-7 Gate A（本文件） | `FROZEN` |
| T-7-A 实现 | `PASS — SCOPE-BOUND / CLOSED` |
| T-7-B EFX | `DEFERRED — SCOPE FROZEN IN docs/T-7-Audio-EFX-Scope.md` |
| Bridge ABI | `NOT AUTHORIZED` |
| Editor 音频集成 | `NOT AUTHORIZED` |

> Gate A 冻结仅表示契约可作为 Gate B 的输入，不表示 T-7-A 已获实施授权。
> （历史说明；T-7-A 已于 2026-10-30 通过验收并关闭，状态见实施记录的 Final Acceptance。）
