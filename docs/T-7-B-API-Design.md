# T-7-B — OpenAL EFX API & Resource Model (Gate B-1 FROZEN)

**状态**：`API / PARAMETERS / RESOURCE MODEL — FROZEN`
**日期**：2026-10-30
**上游**：`docs/T-7-Audio-EFX-Scope.md`、`docs/T-7-Audio-GateA-Contract.md`、
`docs/T-7-B-Implementation-Plan.md`
**授权**：T-7-B Gate B 实施；不覆盖 Git 提交、Stack 1/A0/A1、CPU DSP 接入、
Bridge/Editor ABI 或 `third_party/openal-soft` 修改。

> 本文件冻结实现所需的公共 API、参数范围、Handle 语义和资源依赖；实现不得在未记录
> 的情况下改变这些语义。

---

## 1. 公共类型

新增 `engine/include/Engine/Core/Audio/IAudioEfx.h`。

### 1.1 Capability

```cpp
struct AudioEfxCapabilities {
    bool supported = false;          // EFX 扩展与必要入口可用
    bool routingSupported = false;   // maxAuxiliarySends > 0
    int32 maxAuxiliarySends = 0;     // 设备查询结果
    AudioStatus status = AudioStatus::Unsupported;
};
```

`status` 取值：

- `Ok`：EFX 可用；基础 Engine 仍为 `Ready`。
- `Unsupported`：设备没有 `ALC_EXT_EFX`；基础音频可用。
- `BackendError`：扩展存在但必要 EFX 入口不可用；基础 Engine 保持 `Ready`，EFX 不工作。
- `NotInitialized` / `ShuttingDown` / `DeviceLost`：Engine 状态不允许查询/使用 EFX。

### 1.2 Resource Handle

```cpp
enum class AudioEfxType : uint8 {
    None = 0,
    Reverb,
    AuxiliarySlot,
    LowPassFilter,
};

struct AudioEfxHandle {
    uint32 engineId = 0;      // Engine instance identity
    uint32 generation = 0;    // successful Init generation
    uint32 serial = 0;        // Engine-local monotonic serial
    AudioEfxType type = AudioEfxType::None;

    bool IsValid() const;
    bool operator==(const AudioEfxHandle&) const;
    bool operator!=(const AudioEfxHandle&) const;
};
```

- Handle **不是 native OpenAL ID**；native ID 只保存在 Engine 内部 registry。
- Handle 的四个字段都参与 identity 校验；任何字段不匹配都拒绝。
- Handle 是**非拥有值类型**：复制不改变所有权，析构不调用 AL。
- 显式 `DestroyEfxObject` 或 Engine Shutdown 释放资源。
- 旧 generation 的 Handle 永久失效；Re-Init 后旧 Handle 返回 `NotInitialized`/`NotFound`，
  不得访问新 Context。
- 跨 Engine 的 Handle 返回 `NotFound`，不得调用后端。

### 1.3 Parameters

```cpp
struct AudioReverbParameters {
    float32 density = 1.0f;       // 0..1
    float32 diffusion = 1.0f;     // 0..1
    float32 gain = 0.32f;         // 0..1
    float32 gainHF = 0.89f;       // 0..1
    float32 decayTime = 1.49f;    // 0.1..20 s
    float32 decayHFRatio = 0.83f; // 0.1..2
};

struct AudioLowPassParameters {
    float32 gain = 1.0f;    // 0..1
    float32 gainHF = 1.0f;  // 0..1
};
```

- 所有浮点参数必须 finite；`NaN`/`Inf` 返回 `InvalidArgument`。
- Reverb/Low-pass 越界值**拒绝**，不静默钳制。
- 设置成功后必须通过后端读回；读回与请求在 `1e-4` 绝对误差内才算成功。
- Low-pass 只暴露 `gain` / `gainHF`，**不提供 `cutoffHz`**；不得把 `gainHF` 描述为精确
  物理截止频率。
- Reverb 固定标准 `AL_EFFECT_REVERB`，不支持运行时切换 Echo/EAX Reverb。

---

## 2. 接口

```cpp
class IAudioEfx {
public:
    virtual ~IAudioEfx() = default;

    virtual AudioEfxCapabilities GetEfxCapabilities() const = 0;

    virtual AudioEfxHandle CreateReverb() = 0;
    virtual AudioStatus SetReverbParameters(
        const AudioEfxHandle& effect,
        const AudioReverbParameters& params) = 0;
    virtual AudioStatus GetReverbParameters(
        const AudioEfxHandle& effect,
        AudioReverbParameters& out) const = 0;

    virtual AudioEfxHandle CreateAuxiliarySlot() = 0;
    virtual AudioStatus AttachEffect(
        const AudioEfxHandle& slot,
        const AudioEfxHandle& effect) = 0;
    virtual AudioStatus DetachEffect(const AudioEfxHandle& slot) = 0;

    virtual AudioStatus ConnectAuxSend(
        IAudioSource* source,
        const AudioEfxHandle& slot,
        float32 sendLevel) = 0;
    virtual AudioStatus DisconnectAuxSend(IAudioSource* source) = 0;

    virtual AudioEfxHandle CreateLowPassFilter() = 0;
    virtual AudioStatus SetLowPassParameters(
        const AudioEfxHandle& filter,
        const AudioLowPassParameters& params) = 0;
    virtual AudioStatus GetLowPassParameters(
        const AudioEfxHandle& filter,
        AudioLowPassParameters& out) const = 0;
    virtual AudioStatus BindDirectFilter(
        IAudioSource* source,
        const AudioEfxHandle& filter) = 0;
    virtual AudioStatus UnbindDirectFilter(IAudioSource* source) = 0;

    virtual AudioStatus DestroyEfxObject(const AudioEfxHandle& handle) = 0;
};
```

`OpenALAudioEngine` 同时实现 `IAudioEngine` 与 `IAudioEfx`。`IAudioEngine` 的现有接口
不变；`FakeAudioEngine` 不需要实现 EFX 接口。

### 2.1 创建失败

`CreateReverb` / `CreateAuxiliarySlot` / `CreateLowPassFilter` 失败返回
`AudioEfxHandle{}`（invalid）；具体原因通过 `OpenALAudioEngine::GetLastStatus()` 观察。
不得返回 native ID 0 的伪有效 Handle。

---

## 3. 资源模型与所有权

### 3.1 Engine 内部

```text
AudioEngineRuntime
  ├── efxCapabilities
  ├── effect registry:  serial -> { type, nativeId, generation, params, attachedSlot }
  ├── slot registry:    serial -> { nativeId, generation, attachedEffect }
  ├── filter registry:  serial -> { nativeId, generation, type, params, boundSources }
  ├── aux route registry: sourceNativeId -> { slotHandle, sendFilterNativeId, level }
  └── direct filter registry: sourceNativeId -> filterHandle
```

- Native source/buffer/effect/slot/filter 均由 Engine 拥有。
- Engine 在 Owner Thread / Ready 下创建和删除 native 资源；EFX `Failed`/`ShuttingDown`
  下不得进行普通 AL 创建/删除。
- 内部 Send Gain Filter 不暴露公共 Handle，只存在于 Aux Route registry。
- 公共 Handle 是 Engine identity + generation + serial + type 的逻辑令牌。

### 3.2 一对一约束（首轮）

- 一个 Auxiliary Slot 最多挂载一个 Reverb。
- 一个 Reverb 最多挂载到一个 Slot。
- 一个 Source 最多一个 Aux Send（send index 0）和一个 Direct Filter。
- 不实现 Slot 共享、Slot 串联、Slot 到 Slot 路由、动态效果图。
- 对象数量不设硬编码上限；由后端 allocation 决定，失败返回明确错误。
- registry 不允许覆盖已有 serial。

### 3.3 依赖与销毁

| 对象 | 销毁前必须 |
|---|---|
| Source | 断开 Aux Send、解除 Direct Filter |
| Slot | 无 Source route、未挂载 Effect |
| Reverb | 未挂载到 Slot |
| Low-pass Filter | 无 Source Direct 绑定 |

- `DestroyEfxObject` 在依赖未解除时返回 `InvalidState`，不自动静默解绑。
- Engine `Shutdown` 是统一清理路径，按 §5 顺序自动解绑所有依赖。
- Source 销毁/Deferred Release 必须先关闭该 Source 的 Aux Send 与 Direct Filter。

---

## 4. EFX 路由语义

### 4.1 Aux Send

- 固定 send index `0`。
- `maxAuxiliarySends == 0` 时 `ConnectAuxSend` 返回 `Unsupported`。
- `sendLevel` 先检查 finite，再钳制到 `[0,1]`。
- 实现使用 Source 专属内部 Low-pass Send Gain Filter：
  - `AL_LOWPASS_GAIN = sendLevel`
  - `AL_LOWPASS_GAINHF = 1.0`
- 不复用 Slot Gain 作为 per-source send level。
- 路由更新是“关闭旧路由 → 建立新路由”的受管理操作；失败不得留下半更新 registry。
- `DisconnectAuxSend` 断开 Source → Slot，删除内部 Send Filter；无路由时返回 `Ok`。

### 4.2 Direct Filter

- 使用 Source direct filter 通道，独立于 Aux Send Filter。
- `BindDirectFilter` 在 Source 已有 Direct Filter 时返回 `InvalidState`。
- `UnbindDirectFilter` 无绑定时返回 `Ok`。
- Filter 销毁前必须解除所有 Direct 绑定。

### 4.3 Low-pass

- 仅 `gain` / `gainHF`；不提供伪造的 Hz cutoff。
- 内部 Send Filter 与用户 Direct Filter 分开管理、分开销毁。

---

## 5. Shutdown 顺序（EFX 部分）

Engine Shutdown 在 T-7-A Source/Buffer/Context 清理之前：

1. 断开所有 Source → Slot Aux Send，删除内部 Send Filter。
2. 解除所有 Source Direct Filter 绑定。
3. 销毁全部公共 Low-pass Filter。
4. 解除所有 Slot 上的 Effect。
5. 销毁全部 Effect。
6. 销毁全部 Auxiliary Slot。
7. 继续 T-7-A Source/Buffer/Context/Device 清理。

`Failed` 状态：清空 EFX registry、Handle 全部 inert，不在失效 Context 上执行普通 AL 删除。
`Re-Init`：清空旧 registry，重新查询 capability；旧 Handle 永久失效。

---

## 6. 错误语义

复用 `AudioStatus`：

| 场景 | 状态 |
|---|---|
| EFX Unsupported | `Unsupported`；基础音频保持 Ready |
| EFX 入口缺失 | `BackendError`；基础音频保持 Ready |
| Wrong Thread | `WrongThread`；后端调用 0 |
| Engine Failed | `DeviceLost` |
| Engine Shutdown | `NotInitialized` |
| Handle 跨 Engine / 旧 generation | `NotFound` / `NotInitialized` |
| Handle 类型错误 | `InvalidArgument` |
| 依赖未解除 | `InvalidState` |
| 参数非法 / NaN / Inf / 越界 | `InvalidArgument` |
| 后端创建/设置/读回失败 | `BackendError` 或 `DeviceLost`（按 T-7-A 设备判断） |
| 重复 Detach/Unbind/Disconnect | `Ok`（幂等） |
| 失败后伪造成功 | 禁止 |

EFX `Unsupported`/`BackendError` 不得把基础 Engine 置为 `Failed`，除非同时发生
T-7-A 定义的 device/context 不可恢复错误。

---

## 7. B2～B9 实施切片

1. **B2**：扩展 seam；Engine Init 查询 `ALC_EXT_EFX`、入口、max auxiliary sends；
   capability 缓存；Shutdown/Re-Init 清除/重查。
2. **B3**：Reverb 创建、默认参数、设置/读回、销毁、Handle 失效。
3. **B4**：Slot 创建、一对一 Attach/Detach、销毁依赖。
4. **B5**：Source aux send / direct filter；Source 销毁自动解绑；内部 Send Filter。
5. **B6**：Low-pass 创建、参数、readback、绑定/解绑、销毁。
6. **B7**：生命周期与 Shutdown/Failed/generation/跨 Engine。
7. **B8**：扩展 Fake 后端，E-T01～E-T14，真实设备 EFX smoke（不支持时 SKIP）。
8. **B9**：Debug/Release 全量、T-7-A A-T01～A-T16、112 既有用例、差异审查、记录。

---

## 8. 文件白名单

见 `docs/T-7-B-Implementation-Plan.md` §4。不得修改 Stack 1、`AudioClip`、
`AudioSystem`/`AudioSourceComponent`、Bridge/Editor 或 `third_party/openal-soft`。

---

## 9. 冻结状态

| 项 | 状态 |
|---|---|
| API 名称/返回类型 | FROZEN |
| Reverb 参数/默认值/范围 | FROZEN |
| Low-pass gain/gainHF 语义（无 cutoffHz） | FROZEN |
| Aux send index 0 + Send Gain Filter | FROZEN |
| Handle 身份字段/非拥有语义 | FROZEN |
| 一对一资源约束 | FROZEN |
| Shutdown 顺序 | FROZEN |
| E-T01～E-T14 | FROZEN |
| 实现 | 在 B2 起按切片推进 |

> 实现中若发现 OpenAL 后端行为与本文件冲突，必须先记录并重新裁决，不得静默改变语义。
