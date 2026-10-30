# T-7 Audio — Gate 0 Read-only Architecture & Thread Safety Scan

**状态**：`T-7 GATE 0 — PASS — READ-ONLY COMPLETE`
**日期**：2026-10-30
**基线**：`docs/T-7-Audio-Stage0-Baseline.md`
**后续**：D7-1～D7-6 已于 2026-10-30 冻结到 `docs/T-7-Audio-GateA-Contract.md`；
§4 保留为扫描时的候选输入，不再作为待决状态。
**方法**：只读源码、测试与已存裁决文档；未构建、未运行测试、未启动音频设备、未查询 EFX
扩展、未运行 ASan/TSan、未修改 `third_party/openal-soft`、未执行 Git 写入。

> 本文是扫描记录，不是 Gate A 契约。§4 保留扫描时的候选输入；Gate A 已于 2026-10-30
> 冻结于 `docs/T-7-Audio-GateA-Contract.md`。

---

## 0. 结论摘要

1. **音频没有进入生产编辑器链路。** `bridge/` 中零音频/OpenAL 引用；`editor_avalonia`
   只在 csproj 中复制 `OpenAL32.dll`，没有音频 API；`Application` 没有音频 engine 成员。
   当前唯一构造/使用点是 sandbox 与手动测试程序。
2. **仓库同时编译两套音频栈。** Stack 1 是 `Engine::Audio::AudioEngine` + `AudioSource`
   直接管理 OpenAL；Stack 2 是 `IAudioEngine`/`OpenALAudioEngine`。ADR 已接受 Stack 2
   为 canonical，但 A1 ownership 迁移处于冻结，A0 provisioning 不在当前 HEAD。
3. **没有应用层音频线程或命令队列。** 所有 AL 调用都在调用线程同步执行；
   `OpenALAudioEngine::Update()` 为空，`AudioAssetManager::LoadAsync()` 实际同步。
   Stack 2 只在 `Init()` 的调用线程设置 current context，之后没有线程归属检查。
4. **EFX 不存在，而不是“已实现但未启用”。** 源码零 `alGenEffects` /
   `alAuxiliaryEffectSlot` / `ALC_EXT_EFX` 使用；只有两个 CPU DSP 类，且没有接入
   OpenAL 播放路径。Effect Slot、Filter、Reverb、Echo 均缺失。
5. **音频源码零同步原语。** `engine/src/Audio`、`engine/src/OpenAL` 与音频头文件中
   没有 `std::mutex` / `std::atomic` / `thread_local` / `std::thread`；多个共享容器与
   对象字段未受保护。
6. **生命周期风险高于 EFX 风险。** `OpenALAudioSource` 析构无条件调用 `alDeleteSources`；
   `Shutdown()` 销毁 context 后，外部仍持有的 source/buffer/clip 可能继续触发 AL 调用。
   Buffer/Clip 的“有 context 才删除”保护只判断任意 current context，不判断所属 context。
7. **设备丢失与初始化失败没有状态机。** 没有 `ALC_CONNECTED` / `alcGetError` 恢复路径；
   `CreateBuffer` / `CreateSource` 失败仍返回对象（native ID 为 0），不向调用方传播失败。
8. **自动化音频测试是 112 个 headless 用例。** 它们从不初始化 OpenAL、不创建 device/source/
   buffer；没有并发、EFX、设备丢失、shutdown 顺序或外部资源生命周期测试。
   手动 sandbox 覆盖部分设备路径，但不是 ctest 门禁。

**最高优先级结论**：T-7 不应先在未定义的双栈所有权模型上叠加 EFX。线程/生命周期契约
必须先把 Stack 2 的 ownership 与 shutdown 语义定死；EFX 应作为其后的独立切片。

---

## 1. 当前架构与消费边界

### 1.1 两套并存实现

| 维度 | Stack 1（高层 `Engine::Audio`） | Stack 2（`IAudioEngine` 抽象） |
|---|---|---|
| 入口 | `engine/src/Audio/AudioEngine.cpp` | `engine/include/Engine/Core/Audio/IAudioEngine.h` + `engine/src/OpenAL/OpenALAudioEngine.cpp` |
| 设备/Context owner | `AudioEngine::m_Device/m_Context`（`AudioEngine.h:79-80`） | `OpenALAudioEngine::m_Device/m_Context`（`OpenALAudioEngine.h:52-53`） |
| 是否使用抽象接口 | **否**，直接 `#include <AL/al.h>` / `<AL/alc.h>` | 是 |
| Source 生命周期 | `unordered_map<handle, AudioSource>`（`AudioEngine.h:91`） | `vector<shared_ptr<OpenALAudioSource>>`（`OpenALAudioEngine.h:57`） |
| Buffer owner | `AudioClip::m_BufferID`（`AudioClip.h:135`） | `OpenALAudioBuffer::m_BufferID`（`OpenALAudioBuffer.h:28`） |
| 典型调用方 | `sandbox/src/AudioSystemTest/main.cpp` | `AudioTest`、`AudioPhysicsSandbox`、`CollisionAudioTest`、`MarioDemo`、`AudioSystem` |

两套栈都编译进 `EngineCore`：`engine/CMakeLists.txt:2-33` 同时 glob `src/Audio/*.cpp`
与 `src/OpenAL/*.cpp`；`EngineCore` 链接 `OpenAL::OpenAL`（`:99`）。

### 1.2 生产消费边界

| 位置 | 音频使用 | 证据 |
|---|---|---|
| `bridge/src`、`bridge/include` | **零** audio / OpenAL / `alSource` / `alc` 引用 | 全目录搜索无匹配 |
| `editor_avalonia` | 无音频 API；只复制 `OpenAL32.dll` | `AvaloniaEditor.csproj:36-40` |
| `engine/src/Core/Application.cpp` | 无 `IAudioEngine` / `OpenALAudioEngine` 成员或注册 | 搜索无匹配 |
| `ResourceManager` | 初始化签名仍是 `Init(IGraphicsFactory&)`，不含音频依赖 | `ResourceManager.cpp:24`、`ResourceManager.h:89` |
| sandbox | 4 个 app + `AudioSystemTest` 构造 engine/播放 | `AudioTestApp.cpp:27`、`AudioPhysicsSandboxApp.cpp:378`、`CollisionAudioTestApp.cpp:167`、`MarioDemoApp.cpp:29`、`AudioSystemTest/main.cpp:48` |
| `AudioSourceComponent` | **无生产实例化/挂载点**（仅头文件注释示例） | 全仓无 `make_shared<AudioSourceComponent>` / `AddComponent<...Audio...>` |

因此，T-7 当前的“真实使用面”是 sandbox/手动程序，不是编辑器。任何新增 Bridge ABI、
编辑器音频面板或资源管线接入都需要单独授权。

### 1.3 既有裁决与当前实现状态

- `docs/ADR-Audio-Canonical-Stack.md` §0.5（Accepted，2026-10-04）已确定
  **Stack 2 为 canonical**，Stack 1 不再扩展，且迁移期间不得新增第二条 buffer provenance
  path（`ADR-Audio-Canonical-Stack.md:25-59`）。
- `docs/Audio-Migration-Design-Freeze.md` 已冻结 `AudioClip` 持有
  `shared_ptr<IAudioBuffer>`、`AudioSourceComponent` 注入 `IAudioEngine&`、bypass 收敛边界
  （`:15-21`、`:27-96`、`:100-131`）。
- 同一记录显示：**A0 provisioning 已实现但 MAINLINE-BLOCKED**，A1 ownership migration
  **冻结**（`:3-21`、`:382-391`、`:407-426`）。当前 HEAD `74ad099` 不包含 A0：
  `git merge-base --is-ancestor 239cc5c HEAD` 返回 1；`Application` 无音频注册，
  `ResourceManager::Init` 无音频参数。
- 结论：T-7 不能假装 canonical provisioning 已存在，也不能在未裁决的情况下另造第三套
  ownership。Gate A 必须明确“恢复 A1 / 仅冻结契约 / 仅修 Stack 2 运行时不变量”中的哪一条。

### 1.4 文档与源码差异（只读观察）

- `docs/Audio-Subsystem-Summary.md:70` 声称“设备管理 100%（含设备枚举）”，但当前源码只调用
  `alcOpenDevice(nullptr)`，没有枚举/设备名参数化；以源码为准。
- 同文档 `:78` 称 `AudioEffect.h` 为“预留空壳”，当前 `AudioEffect.cpp` 已有
  `LowPassFilter` / `FIRConvolution` 两个 CPU DSP 实现，但仍不是 EFX，也未接入播放链。
- 同文档 `:87` 的“线程安全中等、`PlayOneShot` 可能从 worker 线程调用、当前无锁保护”
  与源码一致；这是风险提示，不是已实现保证。

---

## 2. 五方向审计

### 2.1 Audio Device

**谁创建/销毁 Device 与 Context**

| 栈 | 创建 | 销毁 | 失败清理 |
|---|---|---|---|
| Stack 2 | `OpenALAudioEngine::Init()`：`alcOpenDevice(nullptr)`（`OpenALAudioEngine.cpp:46`）、`alcCreateContext`（`:53`）、`alcMakeContextCurrent`（`:61`） | `Shutdown()`：清 sources（`:83`）、`alcMakeContextCurrent(nullptr)`（`:86`）、`alcDestroyContext`（`:87`）、`alcCloseDevice`（`:92`） | 打开设备失败返回 false；context/current 失败逐级清理（`:54-68`） |
| Stack 1 | `AudioEngine::Initialize()`：同样三步（`AudioEngine.cpp:32-44`） | `Shutdown()`：清 sources（`:71`）、销毁 context（`:75-82`）、关闭 device（`:85`） | 同样逐级清理 |

**事实与缺口**

- 只使用默认设备 `nullptr`；无设备枚举、设备名参数化、null-driver/headless policy。
- 没有 Application 级 owner；两套 engine 都由 sandbox `shared_ptr` 直接持有。
- `OpenALAudioEngine::CreateBuffer()`（`:103-108`）与 `CreateSource()`（`:110-114`）
  **不检查 `m_Initialized`、不检查 `m_Context`、不显式 `alcMakeContextCurrent`**。
  失败时仍返回对象；`OpenALAudioSource` 构造失败只是把 `m_SourceID` 置 0
  （`OpenALAudioSource.cpp:14-20`），错误不会传播给调用方。
- `OpenALAudioEngine::Shutdown()` 不记录 owner thread、不等待其他线程退出、不检查
  context 是否仍在其他线程 current；`alcMakeContextCurrent(nullptr)` 只影响调用线程。
- 设备丢失：`engine/` 内搜索 `ALC_CONNECTED`、`ALC_DISCONNECTED`、`ALC_EXT_disconnect`、
  `alcGetError`、`device lost`，**零匹配**；`OpenALAudioEngine::Update()` 为空
  （`OpenALAudioEngine.cpp:173-176`），没有健康检查或恢复。

**结论**：设备生命周期只覆盖“正常 Init/Shutdown”路径；没有设备丢失、重连、降级或
owner-thread 约束。初始化失败只在打开设备/context 阶段有清理，资源创建失败没有状态。

### 2.2 Audio Thread

**没有应用层音频线程**

- `OpenALAudioEngine::Update()` 空实现（`OpenALAudioEngine.cpp:173-176`）。
- Stack 1 `AudioEngine::Update(float)` 只清理 stopped source 并打印间接路径日志
  （`AudioEngine.cpp:92-114`）；`ComputeIndirectPaths()` 只周期性打日志（`:284-293`）。
- `AudioAssetManager::LoadAsync()` 名为异步，实际同步调用 `GetClip()` 并立即回调
  （`AudioAssetManager.cpp:180-190`）；没有 `JobSystem` / `std::thread` / 队列。
- 所有 sandbox 在渲染/主循环线程直接调用 `PlayOneShot`、`Update`、`SetPosition`。

**Context 的线程语义**

- `OpenALAudioEngine::Init()` 在调用线程执行 `alcMakeContextCurrent(m_Context)`（`:61`）。
  OpenAL 的 current context 是 per-thread 状态；该调用不会让其他线程自动获得 context。
- Stack 2 后续方法（`CreateBuffer`、`CreateSource`、`SetListener*`、`GetListener*`）
  直接调用 `al*`，没有 `alcMakeContextCurrent` 或线程检查（`:103-167`）。
- `OpenALAudioSource` 构造、`Play`、`Stop`、所有 setter 也直接调用 `al*`
  （`OpenALAudioSource.cpp:14-148`），没有 context 参数或 owner 校验。
- Stack 1 的 `AudioSource::MakeContextCurrent()`（`AudioSource.cpp:40-44`）会在多数 AL
  操作前尝试把保存的 context 设为 current；`ApplyListenerToOpenAL()` 也这样做
  （`AudioEngine.cpp:217-230`）。因此 Stack 1 的 AL 调用不要求调用线程是 Init 线程，
  但前提是保存的 context 仍有效，且没有任何跨线程串行化。

**没有命令传递**

- 不存在 main → audio 或 audio → main 的命令队列、future、promise、ring buffer。
- 不存在“音频线程独占 AL、主线程排队”的模型；也不存在“主线程独占、音频线程只读”的模型。
- 因此“播放线程与主线程之间如何传命令”的当前答案是：**没有传递机制，调用者直接执行**。

**结论**：T-7 若需要专用音频线程，必须从零建立线程、队列、生命周期与关闭协议；
当前代码不能通过加一把锁就获得一个 audio thread。若接受“单 owner thread”模型，
则必须把这一约束写成契约并加运行期/Debug 检查，因为当前完全没有约束。

### 2.3 EFX

**源码事实**

- 全仓（排除 `third_party/`）搜索 `alGenEffects`、`alAuxiliaryEffectSlot`、
  `ALC_EXT_EFX`、`AL_EXT_EFX`、`alEffectf`、`alFilteri`、`alGenFilters`、`AL_EFFECT_`：
  **零代码匹配**。只有 `docs/Audio-Subsystem-Summary.md:110` 把 EFX 列为未来工作。
- `engine/` 内没有 `alcIsExtensionPresent` / `alIsExtensionPresent` 调用。
- 没有 Effect Slot、Auxiliary Effect Slot、Filter、Reverb、Echo 对象。
- 未查询 pinned `third_party/openal-soft`（`9fb7bb38c`）构建的 EFX 导出能力；
  不把“OpenAL Soft 通常支持 EFX”当作本项目已有能力的证据。

**现有的“效果”是什么**

- `AudioEffect` 是 CPU DSP 基类（`AudioTypes.h:82-94`）；
  `AudioEffect.cpp` 只有 `LowPassFilter`（一阶 IIR，`:12-69`）与
  `FIRConvolution`（`:74-142`）。
- `AudioBusManager::ApplyBusProcessing()`（`AudioBusManager.cpp:162-243`）对调用方提供的
  `float*` 缓冲做总线音量、DSP 湿/干混合与发送统计；它**不接触 OpenAL source/buffer**。
- 该函数只在 `tests/test_audio/AudioBusManagerTest.cpp` 与
  `sandbox/src/AudioSystemTest/main.cpp` 被调用；`AudioEngine` 不持有 `AudioBusManager`，
  没有任何引擎播放路径调用它。
- `AudioSource::ConfigureAuxSends()` 只打印日志（`AudioSource.cpp:336-341`），
  没有实际 aux send 或 effect slot 绑定。
- `Listener` 的 `EnvironmentPreset`、`SetRoomParameters()` 只保存字段
  （`Listener.h:42-43`、`:150-164`），`Apply()` 不会把它们发送给 engine
  （`Listener.cpp:163-180`）。

**结论**：当前 EFX 能力为 **0**。不是“EFX 混响未启用”，而是没有 EFX 对象、
没有扩展查询、没有播放路径接入；CPU DSP 也不能被 OpenAL 播放链消费。
T-7 的 EFX 部分本质上是新建功能，不是打开开关。

### 2.4 Resource Lifetime

| 风险点 | 证据 | 后果 |
|---|---|---|
| Source 析构无条件删除 AL source | `OpenALAudioSource.cpp:22-27`：`if (m_SourceID) alDeleteSources(...)`，无 context 检查 | context 已销毁时调用无效 AL；sandbox `AudioTestApp` 的析构顺序可触发（见下） |
| Engine shutdown 不等待外部 owner | `OpenALAudioEngine::Shutdown()` 只 `m_Sources.clear()`（`:83`），不使外部 `shared_ptr<IAudioSource>` 失效 | source 可活过 context；析构时无 context 调用 AL |
| Buffer/Clip 的 context 保护不完整 | `OpenALAudioBuffer.cpp:16-41`、`AudioClip.cpp:11-14,144-156` 只检查 `alcGetCurrentContext()!=nullptr` | 无 context 时跳过删除（泄漏）；有其他 context 时在错误 context 删除（AL 错误/未定义） |
| Buffer/Clip 创建依赖“当前线程有任意 context” | `OpenALAudioBuffer` 构造 `alGenBuffers`（`:23-29`）；`AudioClip::LoadPCM` 的 `alGenBuffers`（`:107`）不显式设置所属 context | 在 worker 线程加载时失败或创建到错误 context；随后绑定到主 context 的 source 时不一致 |
| Stack 1 source 保存裸 context 指针 | `AudioSource.cpp:21-28` 的 `SafeDeleteSource(m_SourceID, m_Context)` 只判空，不判 context 是否仍有效 | context 销毁后析构为悬空指针调用 |
| 裸 `AudioClip*` 所有权 | `AudioEngine` 的 `AudioSource::m_Clip`（`AudioSource.h:101`）、`AudioSourceComponent::m_BoundClip`（`AudioSourceComponent.h:145`）、`OneShotEntry::clip`（`AudioSystem.cpp:18-21`） | Stack 1 的 `AudioSource::Play()` 会解引用 `m_Clip`（`AudioSource.cpp:135-155`）；`SetPaused(false)` 会对所有 source 调 `Play()`（`AudioEngine.cpp:123-132`），clip 释放后可能 UAF。Component/one-shot 暴露悬空指针，one-shot 绑定 native buffer 但不持有 clip |
| `GetSource` 返回裸指针 | `AudioEngine.cpp:187-191`；`Update()` 会 erase stopped source（`:96-113`），`DestroySource()` 也 erase（`:173-185`） | 调用方保存的 `AudioSource*` 在下一帧/下一次销毁后悬空 |
| One-shot 全局静态列表 | `AudioSystem.cpp:24` 的 `s_ActiveOneShots`；`StopAllOneShots()`（`:115-123`）必须由调用方在 engine shutdown 前手动执行 | 未清理时静态 vector 活过 engine，source 析构发生在 context 销毁之后 |
| AssetManager 手工引用计数 | `AudioAssetManager::GetClip()` 递增 `refCount`（`:196-217`），`UnloadClip()` 递减（`:219-234`），`UnloadAll()` 直接清空（`:236-240`） | 计数与 `shared_ptr` 生命周期不一致；可能泄漏或提前释放；无锁 |
| 全局 AssetManager 泄漏 | `g_AssetManager = new AudioAssetManager()`（`:301-310`），无删除 | 进程退出前常驻；若有缓存 clip，buffer 释放顺序不确定 |

**可触发的具体顺序（源码级，不是实测崩溃）**

`AudioTestApp` 的成员声明顺序是 `m_AudioEngine`、`m_Source`、`m_Buffer`
（`AudioTestApp.h:39-41`）。析构函数体先执行：

1. `m_Source->Stop()`（`AudioTestApp.cpp:112-114`）；
2. `m_AudioEngine->Shutdown()`（`:115`）——销毁 context；
3. 随后成员逆序析构，`m_Source` 的 `~OpenALAudioSource()` 仍持有非零 `m_SourceID`，
   执行 `alDeleteSources`（`OpenALAudioSource.cpp:22-27`），此时 context 已销毁。

这是现有 sandbox 代码路径，但 Gate 0 未运行程序，因此只声明“源码顺序上成立”，
不宣称已观测到崩溃。`AudioPhysicsSandbox` 则手动在 `Shutdown()` 前
`StopAllOneShots`、`DestroySource`、`Release` clips（`AudioPhysicsSandboxApp.cpp:406-481`），
说明正确顺序当前依赖每个调用方自觉遵守，而不是 engine 强制。

### 2.5 Thread Safety

**同步原语审计**

在 `engine/src/Audio`、`engine/src/OpenAL`、`engine/include/Engine/Audio`、
`engine/include/Engine/Core/Audio`、`engine/include/Engine/OpenAL` 中搜索
`std::thread`、`std::jthread`、`std::mutex`、`std::shared_mutex`、`std::atomic`、
`thread_local`、`condition_variable`：**零匹配**。

**未保护的共享状态**

| 对象 | 共享状态 | 并发风险 |
|---|---|---|
| `OpenALAudioEngine` | `m_Sources` vector（`.h:57`）；`Init/Shutdown/CreateSource/DestroySource` 修改 | 并发 create/destroy/shutdown 的数据竞争；`shared_ptr` vector 重分配与遍历竞争 |
| `Engine::Audio::AudioEngine` | `m_Sources` map、`m_SourcePool` vector、`m_NextHandle`（`.h:90-93`） | `Update/Create/Destroy/PlayOneShot/SetPaused/GetActiveVoiceCount` 无锁；`GetSource` 裸指针 |
| `AudioSystem` | 静态 `s_ActiveOneShots`（`.cpp:24`） | 多线程 PlayOneShot/Update/StopAll 竞争；无 shutdown hook |
| `AudioAssetManager` | `m_Cache`、`refCount`、`m_HotReload` | 加载/卸载/热重载并发竞争；`LoadAsync` 假异步 |
| `AudioBusManager` | `m_Buses`、`m_Effects`、`sends` 裸指针 | 主线程改总线/效果与“音频线程”处理并发会竞争；`ApplyBusProcessing` 还会分配 vector |
| `SpatialAudioManager` | `m_Sources`、几何、回调、参数 | 主线程改几何/参数与计算线程并发会竞争；回调可重入/生命周期未定义 |
| `OpenALAudioSource` | `m_SourceID`、`m_Buffer`、`m_Volume`、`m_Pitch`、`m_Looping` | 播放中改参/Stop/析构无保护；`m_Buffer` shared_ptr 并发读写不安全 |
| `AudioClip` | `m_BufferID`、`m_Info`、Resource 状态 | 加载/Release/播放 cross-thread 竞争 |

**线程语义的不一致**

- Stack 2 假设调用线程已经由 `Init()` 设置 current context；不设置也不检查。
- Stack 1 在多个方法中主动 `alcMakeContextCurrent(m_Context)`，因此“看起来”能跨线程，
  但保存的 context 生命周期、并发调用串行化、对象字段竞争都未处理。
- `alcMakeContextCurrent` 只改变调用线程的 current context，不是互斥锁；不能替代
  对 `m_Sources`、`m_Buffer`、`refCount` 等对象状态的保护。
- `OpenALAudioEngine::Shutdown()` 可在任意线程调用；`alcMakeContextCurrent(nullptr)`
  只清除该线程的 current context，其他线程可能仍在使用/持有 context。

**当前是否真的跨线程使用**

- 现有 sandbox 调用点都在主循环/主线程；没有找到音频 worker 线程。
- 但 API 与文档没有禁止跨线程：`Audio-Subsystem-Summary.md:87` 明确写
  “`PlayOneShot` 可能从 worker 线程调用。当前无锁保护”；`AudioAssetManager.cpp:183`
  也留下“如果在主线程有 JobSystem”的注释。
- 因此这些是**潜在但未被当前调用点约束**的风险；Gate 0 未运行并发压力测试，不宣称
  已复现数据竞争。

### 2.6 Device 丢失与初始化失败

- 设备丢失：无 `ALC_CONNECTED` 查询、无 `alcGetError` 健康检查、无 `Update()` 恢复、
  无 Lost 状态、无资源失效/重建流程；`OpenALAudioEngine::Update()` 为空
  （`OpenALAudioEngine.cpp:173-176`）。
- 初始化失败：`Init()` 对 `alcOpenDevice/alcCreateContext/alcMakeContextCurrent` 有
  失败清理（`:46-68`），但 `CreateBuffer/CreateSource` 不检查 `m_Initialized`，
  失败仍返回对象；`OpenALAudioSource` 构造失败只把 ID 置 0（`OpenALAudioSource.cpp:14-20`）。
- `AudioSystem::PlayOneShot()` 不检查 engine 是否已初始化；它直接取
  `source->GetNativeHandle()` 并调用 `alSourcei/alSourcePlay`（`AudioSystem.cpp:45-66`）。
  当 source ID 为 0 时，AL 调用失败但只记录日志。
- `AudioClip::LoadFromFile()` 在无 current context 时直接失败（`AudioClip.cpp:43-47`），
  没有排队/稍后加载/重试机制；在错误 context 下则可能创建到错误 context。
- 结论：当前没有“设备丢失”语义；只有“Init 失败返回 false”和若干未传播的资源创建失败。

### 2.7 关闭与异步任务

- 没有音频异步任务：`AudioAssetManager::LoadAsync()` 同步执行（`:180-190`）；
  没有 JobSystem、线程池或 completion queue。
- 没有 engine 级 shutdown hook 负责清理音频：`Application` 无音频注册；
  Stack 1/2 的 `Shutdown` 只清自己的 owner 容器，不处理外部 `shared_ptr`。
- `AudioSystem` 的全局 one-shot 列表没有注册到任何 engine shutdown；
  调用方必须先 `StopAllOneShots` 再 `Shutdown`。
- `AudioTestApp` 的顺序问题见 §2.4；`AudioPhysicsSandbox` 手动正确排序，说明顺序是
  调用方约定，不是框架保证。
- 结论：T-7 若引入音频线程/异步加载，必须同时定义“关闭时如何取消/排空任务、
  owner thread 等待、资源销毁顺序、外部 owner 失效语义”；当前没有这些基础。

### 2.8 测试覆盖

**自动化测试**

`tests/CMakeLists.txt:259-268` 注册 `test_audio`，源文件为：

- `AudioBusManagerTest.cpp`（27 个 `TEST`）
- `SpatialAudioManagerTest.cpp`（21）
- `AudioAssetManagerTest.cpp`（19）
- `ListenerTest.cpp`（29）
- `AudioLoaderTest.cpp`（16）

合计 **112** 个 `TEST`。审计事实：

- 这 5 个文件没有任何 `.Init()`、`alcOpenDevice`、`alcCreateContext`、`alGenSources`、
  `alGenBuffers` 调用——测试从不初始化 OpenAL 或创建真实资源。
- `AudioBusManagerTest.cpp:3-7` 自述三个 manager “零 AL/ALC 调用点”，可在无设备 CI 运行；
  `AudioAssetManagerTest.cpp:14-16` 明确把 `refCount` 等 device-bound 语义留待后续；
  `ListenerTest.cpp:23-26` 明确把 `AudioSystem`/`AudioSourceComponent` 的 native 绕过
  排除在测试范围外。
- 没有任何 `std::thread` / `std::mutex` / `std::atomic` / `std::async` 用例。
- 没有 EFX、设备丢失、shutdown 顺序、source-outlives-context、裸 clip 生命周期测试。
- `test_audio` 链接 EngineCore 并需要 OpenAL32.dll（`tests/CMakeLists.txt:267-278`），
  但这只是导入依赖，不代表测试使用设备。

**手动 sandbox**

- `AudioTest`：真实 device + `OpenALAudioEngine`，手动播放；析构顺序存在 §2.4 风险。
- `AudioSystemTest`：真实 device + Stack 1，覆盖生命周期/属性/总线/DSP 的部分路径；
  输出 PASS/FAIL 日志，但不是 ctest 门禁，也没有并发/设备丢失。
- `AudioPhysicsSandbox`、`CollisionAudioTest`、`MarioDemo`：手动 app，覆盖 one-shot、
  3D 位置与资源释放顺序；不是自动化门禁。
- 结论：现有测试证明的是“无设备数学/管理逻辑”，不是“音频运行时、线程或生命周期”。

---

## 3. 风险登记

| ID | 严重度 | 风险 | 关键证据 | 当前覆盖 |
|---|---|---|---|---|
| R-01 | **Critical** | Stack 2 的 context 只在 Init 线程 current；后续 CreateBuffer/CreateSource/Listener/Source 操作无 owner-thread 检查或 current-context 设置 | `OpenALAudioEngine.cpp:61,103-167`；`OpenALAudioSource.cpp:14-148`；`OpenALAudioBuffer.cpp:23-29` | 无 |
| R-02 | **Critical** | Context 销毁后仍可能有外部 source/buffer/clip 存活并调用 AL；`OpenALAudioSource` 析构无 context 检查 | `OpenALAudioEngine.cpp:82-97`；`OpenALAudioSource.cpp:22-27`；`AudioTestApp.h:39-41`、`AudioTestApp.cpp:111-116` | 无 |
| R-03 | **High** | Buffer/Clip 的“有 context 才删除”保护只判断任意 current context，可能漏删或删错 context；创建不显式设置 context | `OpenALAudioBuffer.cpp:16-41`；`AudioClip.cpp:11-14,100-156` | 无 |
| R-04 | **High** | 裸 `AudioClip*` 所有权：Stack 1 source 会解引用；`AudioSourceComponent`/one-shot 暴露悬空指针或绑定失效 buffer | `AudioSource.h:101`；`AudioSourceComponent.h:145`；`AudioSystem.cpp:18-21,75`；`AudioSource.cpp:135-155`；`AudioEngine.cpp:123-132` | 无 |
| R-05 | **High** | 音频对象/容器完全没有同步；并发 create/destroy/play/stop/参数修改存在数据竞争 | 音频源码零 mutex/atomic；`OpenALAudioEngine.h:57`；`AudioEngine.h:90-93`；`AudioSystem.cpp:24` | 无 |
| R-06 | **High** | EFX 完全缺失：无扩展查询、Effect Slot、Filter、Reverb/Echo；CPU DSP 未接入播放链 | `AudioEffect.cpp:12-158`；`AudioBusManager.cpp:162-243`；`AudioSource.cpp:336-341`；`Listener.h:150-164` | 仅 CPU DSP 单测 |
| R-07 | **High** | 双栈并存且线程模型不一致；ADR 已选 Stack 2，但 A0 不在 HEAD、A1 冻结；EFX/线程修复若两栈都做会翻倍并违反 ADR | `ADR-Audio-Canonical-Stack.md:25-59`；`Audio-Migration-Design-Freeze.md:3-21,382-426`；当前 `Application` 无音频 | 无 |
| R-08 | **Medium-High** | 设备丢失无检测/恢复；CreateBuffer/CreateSource 失败不传播；`Update()` 空 | 设备丢失关键词零匹配；`OpenALAudioEngine.cpp:103-114,173-176`；`OpenALAudioSource.cpp:14-20` | 无 |
| R-09 | **High** | 自动化测试完全不覆盖 device、并发、EFX、shutdown 顺序与外部资源生命周期 | `tests/CMakeLists.txt:259-268`；5 个测试文件零 AL 初始化；测试注释自述 device-bound 缺口 | 112 headless tests |
| R-10 | **Medium** | `AudioSourceComponent::Play()` 先手动绑定 buffer，再调用 `Play(nullptr)`；`OpenALAudioSource::Play` 对 null buffer 直接 return，因此接口调用是 no-op，实际播放依赖裸 AL 调用 | `AudioSourceComponent.cpp:38-54`；`OpenALAudioSource.cpp:29-40` | 无（且该组件当前无实例化点） |
| R-11 | **Medium** | `AudioAssetManager` 手工 refCount、`LoadAsync` 同步、`UnloadAll` 忽略引用、全局实例不释放；无锁 | `AudioAssetManager.cpp:180-240,301-310` | 仅空缓存/失败路径单测 |
| R-12 | **Medium** | Stack 2 `m_Sources` 只在显式 Destroy 时移除；`Update()` 不回收 stopped source，长期 Create 不 Destroy 会累积 | `OpenALAudioEngine.cpp:110-124,173-176` | 无 |

严重度说明：Critical/High 指“一旦按当前公开 API 跨线程或跨 shutdown 使用，契约未定义且
可能造成无效 AL 调用/资源错误”；不是宣称现有 sandbox 主线程路径已经崩溃。

---

## 4. Gate A 候选输入（扫描时记录；已由 Gate A 冻结取代）

> 2026-10-30 裁决已将 D7-1～D7-6 冻结到 `docs/T-7-Audio-GateA-Contract.md`。
> 本节保留扫描时的候选输入与决策问题，不再代表当前待决状态。

### 4.1 与既有裁决的关系

任何 T-7 方案都必须先回答：

1. T-7 是否以 **Stack 2 为唯一目标**（ADR 已接受）？
2. 是否恢复/消费 A0 provisioning 与 A1 ownership migration，还是只冻结运行时不变量？
3. EFX 是否只允许加在 Stack 2？**不得**在 Stack 1 新增 EFX 或第二套 buffer provenance。

当前建议：**T-7-A 只做 Stack 2 的执行/生命周期契约与最小修复；T-7-B 再做 EFX。**
在 A1 未恢复前，不把 `AudioSourceComponent` / `AudioSystem` 的新功能当作 T-7 成果。

### 4.2 候选 T-7-A：执行模型与生命周期

推荐的最小契约（供 Gate A 讨论，不是已批准内容）：

- **单 owner thread**：`IAudioEngine` 的 Init/Shutdown/CreateBuffer/CreateSource/所有
  source 操作只允许在创建 engine 的线程调用；Debug 下记录 owner thread ID 并拒绝
  跨线程调用，Release 下返回明确错误（不得静默执行）。
- **不引入专用音频线程/命令队列**：T-7-A 先固定“主线程同步调用”模型；若未来需要
  worker 加载，完成回调必须 marshal 回 owner thread，不得在 worker 直接 AL 调用。
- **Engine 独占 context 生命周期**：Shutdown 先进入 `ShuttingDown`（拒绝新调用），
  再停止/销毁所有 source、buffer、clip，最后清 current context、销毁 context、关闭 device。
  资源析构不得在 context 销毁后调用 AL；外部 `shared_ptr` 必须通过 engine 的
  invalidation/generation 语义失效，或明确禁止活过 Shutdown。
- **失败语义**：`CreateBuffer`/`CreateSource` 失败返回 `nullptr`/状态，不返回 ID=0 的对象；
  Init 失败进入 `Uninitialized`；设备丢失进入 `Lost`（fail-closed），T-7-A 不做自动恢复。
- **所有权收敛**：按 `Audio-Migration-Design-Freeze.md` 的 A1 结论，`AudioClip` 持有
  `shared_ptr<IAudioBuffer>`；`AudioSourceComponent` 注入 `IAudioEngine&`；
  `AudioSystem`/`AudioSourceComponent` 的裸 `alSourcei/alSourcePlay` 改为
  `IAudioSource::Play(buffer)`；不得新增第二条 buffer 来源。
- **Stack 1 处置**：不再扩展；若保留，必须标记 legacy/frozen，且不得成为 EFX 的实现目标。
  是否在 T-7-A 删除 Stack 1 需单独裁决（涉及 Spatial/occlusion/cone 能力迁移）。
- **测试**：先建立 headless FakeAudioEngine/FakeAudioBuffer 的 ownership/state 测试，
  再加 device-backed 可选门禁；跨线程用例断言“被拒绝”，而不是制造数据竞争。

### 4.3 候选 T-7-B：EFX

在 T-7-A 契约稳定后，最小 EFX 范围建议：

- 查询 `ALC_EXT_EFX` / `AL_EXT_EFX`，不支持时明确降级（不静默假装有混响）。
- 创建/销毁 Effect Slot、Auxiliary Effect Slot、Filter；至少覆盖 Reverb、Low-pass、
  Echo 中的最小集合；参数钳制与错误返回。
- 将 source aux send 真正绑定到 effect slot；将 Listener 的 environment/room 参数
  映射到 EFX（或明确保留为未实现字段，不伪造）。
- 效果只在 Stack 2 播放链生效；CPU `AudioBusManager` 的 DSP 是否保留为离线/总线处理
  需单独决定，不能与 EFX 混成第二套效果链。
- EFX 测试在无 EFX/无设备时明确 SKIP 并记录原因，不能把 SKIP 当 PASS。

### 4.4 候选测试矩阵

| 层 | 用例 | 环境 | 预期 |
|---|---|---|---|
| Headless contract | Init/Shutdown 状态机；double shutdown；call-after-shutdown | FakeAudioEngine/FakeAudioBuffer，无设备 | 确定、无 AL 调用 |
| Headless lifecycle | AudioClip 持有 buffer；source 持有 buffer；clip 释放后播放不失效 | Fake backend | shared_ptr retention 可断言 |
| Headless ownership | `AudioSourceComponent` 注入 engine；`AudioSystem` 走 `IAudioSource::Play` | Fake backend | 无裸 `alSourcei` 路径 |
| Thread contract | owner 线程外调用 Create/Play/Stop/Shutdown | Fake backend + 线程 | 明确拒绝/错误；不产生数据竞争 |
| Device-backed | Init/Create/Play/Stop/Shutdown; source/buffer 释放顺序 | 真实 OpenAL Soft 默认设备 | 通过；失败/无设备时记录 SKIP，不冒充 PASS |
| Device loss | 设备断开/恢复行为（若可安全模拟） | 独立手动/受控环境 | 进入 Lost/fail-closed；无崩溃；恢复另行裁决 |
| EFX | extension query、effect slot/filter 创建销毁、参数、aux send | 设备 + EFX 支持 | 通过；不支持时明确 SKIP |
| Regression | `test_audio` 112、现有 sandbox smoke、T-6/T-1 不受影响 | Debug/Release 分开 | 保持通过；不宣称零警告 |

### 4.5 需要裁决的问题

| # | 问题 | 影响 |
|---|---|---|
| Q-1 | T-7 是否只做 Stack 2，Stack 1 仅冻结/最终退休？ | 决定是否允许继续双栈；EFX 只能有一套 |
| Q-2 | 接受“单 owner thread、无专用音频线程”吗？ | 决定是否需要命令队列/线程设计 |
| Q-3 | 是否恢复 A0/A1，还是 T-7-A 仅修 Stack 2 运行时不变量？ | 决定 `AudioClip`/`AudioSourceComponent`/`AudioSystem` 是否在范围内 |
| Q-4 | 设备丢失是 fail-closed 还是自动恢复？ | 决定状态机与测试范围 |
| Q-5 | EFX 最小集合（Reverb/Low-pass/Echo）与是否保留 CPU DSP 总线？ | 决定 T-7-B 范围与效果链归属 |
| Q-6 | 是否允许任何 Bridge/编辑器音频 ABI？ | 当前无 ABI；默认不允许 |
| Q-7 | 外部 `shared_ptr` source/buffer 是否允许活过 engine Shutdown？ | 决定销毁/失效契约；建议禁止或强失效 |

---

## 5. 本轮明确不做 / 未验证

- 未构建、未运行 `test_audio`、未运行 sandbox、未启动音频设备。
- 未查询 EFX 扩展、未创建 device/context、未做设备丢失注入。
- 未运行 ASan/TSan/并发压力测试；本文的并发结论来自源码结构与缺失的同步原语，
  不是已复现的数据竞争。
- 未审计 `third_party/openal-soft` 内部实现，不对其内部线程/锁行为作断言。
- 未修改 `engine/`、`bridge/`、`tests/`、`sandbox/`、`third_party/`；未执行 Git 写入。
- 不新增 Bridge ABI、不接编辑器音频面板、不改变 T-6-A 行为、不改变 UI-H 状态。
- 本文的候选范围已被 2026-10-30 Gate A 冻结取代（`docs/T-7-Audio-GateA-Contract.md`）；
  本文不把扫描时的候选输入当作契约本身。
