# ADR: 音频 canonical stack 选择

> **状态**: 待决策（Decision Pending）— 本文档是**决策输入**，不含结论
> **最后更新**: 2026-10-04
> **涉及范围**: `engine/src/Audio/`、`engine/src/OpenAL/`、`engine/include/Engine/Audio/`、`engine/include/Engine/Core/Audio/`
> **关联文档**: `docs/Audio-Subsystem-Summary.md`（已记载"双栈架构"）

---

## 0. 本文档的定位

本文档**不是**架构批准记录。它只做三件事：

1. 陈列**已验证的现状事实**（附源码位置）
2. 列出**可行选项**
3. 分析**每个选项的后果**

**本文档刻意不给出推荐选项。** 任何"推荐"都会使后续决策者把分析误读为已批准架构。
选择哪个 stack 会改变所有权、生命周期、有效性语义与公开 API —— 这属于产品/架构意图。

---

## 1. 现状事实

以下每条均可由源码位置直接核对。

### 1.1 存在两个独立的 OpenAL 实现

| | Stack 1 | Stack 2 |
|---|---|---|
| 入口 | `engine/src/Audio/AudioSource.{h,cpp}`（实现 344 行） | `engine/include/Engine/Core/Audio/IAudioEngine.h` + `engine/src/OpenAL/OpenALAudioEngine.cpp`（178 行） |
| 抽象 | **无**。直接 `#include <AL/al.h>`、`<AL/alc.h>` | `IAudioEngine` / `IAudioSource` / `IAudioBuffer` 纯虚接口 |
| 源句柄 | 自有裸 `ALuint m_SourceID` | `IAudioSource::GetNativeHandle()`（封装在实现内） |
| AL context | 自管（`MakeContextCurrent()`） | 由 `OpenALAudioEngine::Init()` 管理 |
| 直接调用 | `alSourcei` / `alSourcePlay` / `alSourcePause` | 封装在 `OpenALAudioSource` 内 |

Stack 1 **完全不经过** `IAudioEngine` / `IAudioSource`。

### 1.2 Stack 2 的 `Play()` 实现是正确抽象的

`engine/src/OpenAL/OpenALAudioSource.cpp:29`：

```cpp
void OpenALAudioSource::Play(std::shared_ptr<IAudioBuffer> buffer) {
    if (!m_SourceID || !buffer) return;   // null → 静默 no-op
    m_Buffer = buffer;                     // 持有 shared_ptr
    alSourcei(m_SourceID, AL_BUFFER, buffer->GetNativeHandle());
    alSourcePlay(m_SourceID);
    ...
}
```

`Stop()` 中 `m_Buffer.reset()`（L46）。即 **Stack 2 已经具备 buffer 生命周期管理**。

### 1.3 三处消费 `AudioClip` 的原生句柄

`AudioClip::GetBufferHandle()` 的全部调用点（仓库范围 874 文件扫描）：

| 位置 | 所属 | 机制 |
|---|---|---|
| `engine/src/Audio/AudioSource.cpp:142` | Stack 1 | `alSourcei(m_SourceID, AL_BUFFER, clip.GetBufferHandle())` |
| `engine/src/Audio/AudioSourceComponent.cpp:38` | Stack 2 | 手动 `alSourcei` 绑定 |
| `engine/src/Audio/AudioSystem.cpp:45` | Stack 2 | 手动 `alSourcei` 绑定 |

另有**文档层面**的处方：`engine/include/Engine/Core/Audio/AudioClip.h:22` 的注释直接示范
`alSourcei(source, AL_BUFFER, clip->GetBufferHandle())` —— 即绕过抽象是**被文档化鼓励**的做法。

`AudioSourceComponent::Play` 在手动绑定后调用 `m_Source->Play(nullptr)`，而该调用因 null 判断**直接返回、不做任何事**。也就是说该组件对接口的调用在构造上就是 no-op，实际播放完全依赖前面的裸 OpenAL 调用。

### 1.4 绕行原因已定位（历史）

`engine/src/Audio/AudioSystem.cpp:64` 注释：

> buffer 已通过 alSourcei 绑定，不能传 nullptr 给 IAudioSource::Play，它会在 !buffer 处拒绝

即：**`AudioClip` 无法提供 `IAudioBuffer`**，而 `IAudioSource::Play()` 拒绝空 buffer，两者叠加导致作者选择取原生句柄。**这是一个已知约束，不是随手写坏。**

### 1.5 `AudioClip` 自身与 OpenAL 耦合

`engine/src/Audio/AudioClip.cpp` 直接 `#include <AL/al.h>`、`<AL/alc.h>`：

- `LoadPCM()`（L100）要求全局 AL context（`HasALContext()`，L102）
- 自行调用 `alGenBuffers`（L107）/ `alBufferData`（L128）
- 上传后**丢弃 PCM**，仅保留 `uint32 m_BufferID` + `AudioClipInfo m_Info`（`AudioClip.h:135-136`）
- `IsValid()` 定义为 `m_BufferID != 0 && m_Info.dataSize > 0 && IsLoaded()`（`AudioClip.h:98-100`）

即：**`AudioClip` 的"有效性"语义由原生句柄定义。**

### 1.6 `m_BufferID` 是活的 production contract

**5 处生产环境 clip 创建点**：
`AudioAssetManager.cpp:48`、`AudioAssetManager.cpp:286`、`AudioClipManager.cpp:21`、`AudioClipManager.cpp:94`、`ResourceManager.cpp:118`（通用 Resource 路径）

**有效性被下游依赖**：`AudioSourceComponent.cpp:32`、`AudioSystem.cpp:33`、`AudioClip.cpp:53/84/87`

**销毁时序被外部协调**：`Release()` 由 sandbox 显式调用
（`AudioPhysicsSandboxApp.cpp:426`、`:434`），需在 OpenAL shutdown 之前释放原生 buffer

### 1.7 覆盖现状

`test_audio`（112 tests）覆盖：Stack 1 的三个 manager（`AudioBusManager` / `SpatialAudioManager` / `AudioAssetManager`）+ Stack 2 的接口侧（`Listener` / `AudioLoader`）。

**未覆盖**：`AudioSource`、`AudioClip`、`AudioClipManager`、`AudioSourceComponent`、`AudioSystem` one-shot、`OpenALAudioEngine` 后端。

### 1.8 后端需要真实设备

`OpenALAudioEngine::Init()` 调用 `alcOpenDevice(nullptr)`（默认设备），无 null-device 分支、无设备名参数化。仅被 4 个 sandbox app 使用，从未被测试使用。

---

## 2. 可行选项

### 选项 A — 以 Stack 2（`IAudioEngine`）为 canonical

统一到接口抽象，消除全部 `ALuint` 绕行。

### 选项 B — 以 Stack 1（`AudioSource`）为 canonical

以现有直接实现为准，把 `IAudioEngine` / `IAudioSource` 视为冗余抽象。

### 选项 C — 共存（现状形式化）

两套栈长期并存，各自服务不同调用方。

---

## 3. 每个选项的后果

### 选项 A 的后果

| 维度 | 后果 |
|---|---|
所有权 | buffer 所有权需从 `AudioClip` 转移到 `IAudioEngine` / `IAudioSource`。`AudioClip` 不再拥有原生 buffer |
生命周期 | 已有正确实现（`m_Buffer` shared_ptr）可直接复用；当前 one-shot 路径不持有 buffer 引用，迁移后**安全性提升** |
有效性语义 | `AudioClip::IsValid()` 依赖 `m_BufferID != 0`，必须重新定义（例如改为 PCM 有效性或 buffer 句柄由引擎侧持有）。**这是对外可见的行为变更** |
公开 API | 需要新增 `AudioClip → IAudioBuffer` 的产出路径；`AudioSourceComponent` 需额外持有 `IAudioEngine&`（当前只有 `IAudioSource`）；`GetBufferHandle()` 是否保留需定 |
内存 | 若选择"保留 PCM + 交由引擎创建 buffer"，PCM 将同时驻留内存与 OpenAL，**内存占用上升** |
测试性 | one-shot 生命周期首次可在无设备条件下测试 |
风险 | 半迁移会同时存在两条 buffer 来源路径，**比现状更差**；必须一次性完成迁移或明确删除旧路径 |

### 选项 B 的后果

| 维度 | 后果 |
|---|---|
所有权 | 维持现状：`AudioClip` 继续拥有原生 buffer |
生命周期 | 无变更；`Release()` 与 shutdown 的时序约束继续存在 |
有效性语义 | 无变更，`IsValid()` 仍依赖原生句柄 |
公开 API | `IAudioEngine` / `IAudioSource` 需标记为非 canonical 或移除；`Listener` / `AudioLoader` 已基于接口的 **112 个测试需要重新定位** |
测试性 | one-shot 与 backend **仍然无法 headless 测试**（依赖真实设备与全局 AL context） |
风险 | 已建成的接口侧测试面（`Listener` 的注入式 `Apply` 验证）失去依托 |

### 选项 C 的后果

| 维度 | 后果 |
|---|---|
所有权 | 双份。同一 clip 可能被两条路径以不同方式解释 |
生命周期 | 两种语义并存：Stack 2 由 source 持有，Stack 1 由调用方持有 |
有效性语义 | `m_BufferID` 成为**永久契约**而非过渡实现 |
公开 API | 两套抽象同时是公开的，使用者需自行选择，**无权威指引** |
测试性 | 与现状一致，无改善 |
风险 | `AudioClip.h:22` 的注释会持续引导新代码走绕过路径；`AudioSourceComponent::Play` 的 no-op 接口调用会持续存在 |

---

## 4. 未决问题

以下问题的答案会显著改变上述后果评估，但目前**无证据可判断**：

1. **沙箱/编辑器/bridge 之外，是否存在外部消费者依赖 `clip.GetBufferHandle()`？**（决定该 API 是否已是公开契约）
2. **`AudioClip` 是否被期望在无 AL context 的环境下加载？**（例如 headless 资源预热；现状是 `LoadPCM` 直接失败）
3. **保留 PCM 的内存预算是否可接受？**（选项 A 的关键成本）
4. **`IAudioEngine` 是为将来替换后端而设，还是仅供 sandbox 使用？**（决定选项 B 是否可行）
5. **是否存在把音频从 GameObject/Component 模型迁移到 ECS 模型的计划？**（与 B4 ADR 相关联）

---

## 5. 本文档明确不做的事

- 不推荐任何选项
- 不宣称任何选项"更干净"或"更现代"
- 不把"`AudioClip` 应该提供 `IAudioBuffer`"写成技术事实 —— 这正是待决内容之一
- 不修改任何代码

---

## 6. 相关记录

| 记录 | 位置 |
|---|---|
P1 headless 音频覆盖（已交付，不因本文档失效） | `ea195d1` |
`test_audio` 当前 112 tests | `tests/test_audio/` |
本文档提到的所有源码位置 | 见各节括注 |