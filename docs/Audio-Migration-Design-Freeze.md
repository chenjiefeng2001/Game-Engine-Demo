# Audio Migration Design Freeze（Stack 2 ownership 冻结）

> **状态**: 设计冻结（Design Frozen）— **Phase A 已解锁**，实施边界见 §6.7
> **上游**: `docs/ADR-Audio-Canonical-Stack.md` §0.5（Accepted, 2026-10-04）
> **性质**: 本文只冻结 ownership / provenance 决策，**不含任何代码改动**
> **本文档不是 ADR**，是实施前的设计定稿

---

## 0. 审计范围与结论摘要

只读审计覆盖 `engine/src/Audio/`、`engine/src/OpenAL/`、`engine/include/Engine/Audio/`、`engine/include/Engine/Core/Audio/`。

三个问题**已全部冻结**，无遗留未决选择：

| 问题 | 冻结结论 |
|---|---|
1. `AudioClip` ownership | **canonical buffer = `AudioClip` 持有 `shared_ptr<IAudioBuffer>`**；PCM 仅作上传中间态，不长期保留 |
2. `AudioSourceComponent` engine context | **构造期注入 `IAudioEngine&`，不引入 singleton**；现有 `PlayOneShot(engine, clip)` 已证明该形态可行 |
3. bypass 收敛边界 | 27 处 native 调用分类完成（见 §3），canonical backend 13 处保留，其余 14 处分四类处置 |
4. `IAudioEngine` 注入边界（§6，架构评审追加） | **注入边界已存在于 committed `Application` subsystem registration**（`Resources` phase lambda）；**provisioning 冻结为 Platform phase + `Application` member + 扩展现有 `FakeAudioEngine`** |

> **Phase A 已解锁**：§6.5 冻结三项 provisioning 决定，§6.7 给出实施边界。注入边界不需要 HRC-3 才存在 —— 初版"不存在注入入口"的结论已在 §6.4 更正。

---

## 1. 冻结结论一：`AudioClip` 的 canonical buffer ownership

### 1.1 决策

> **`AudioClip` 持有 `std::shared_ptr<IAudioBuffer>` 作为唯一 canonical buffer。**
> `m_BufferID`（`uint32`）在迁移期间保留为**派生只读视图**，不再是 owner。

### 1.2 依据

审计发现当前存在**两份完全重复的 buffer 实现**：

| 位置 | 实现 | 状态 |
|---|---|---|
`AudioClip::LoadPCM`（`AudioClip.cpp:100-142`） | `alGenBuffers` + `alBufferData` + duration 计算 | 生产路径，持有 `m_BufferID` |
`OpenALAudioBuffer::Load`（`OpenALAudioBuffer.cpp`） | `alGenBuffers` + `alBufferData` + duration 计算 | **无 `AudioClip` 生产调用方** |

两者逻辑等价。`IAudioEngine::CreateBuffer(pcmData, dataSize, info)`（`IAudioEngine.h:38`）已经封装了后者，返回 `shared_ptr<IAudioBuffer>`。**canonical 实现已存在且已就绪**，`AudioClip` 只是尚未使用它。

需澄清一处：`CreateBuffer` 并非无人调用 —— 4 个 sandbox 直接使用它绕开 `AudioClip` 自持 PCM（`AudioTestApp.cpp:79`、`MarioDemoApp.cpp:242` 与 `:278`、`AudioPhysicsSandboxApp.cpp:530`），`ListenerTest.cpp:55` 另有 stub 实现。

**这构成一个已存在的第二 provenance path**：sandbox 走 `PCM → CreateBuffer → source->Play(buffer)`，完全不经 `AudioClip`；而 `AudioClip` 走 `PCM → 自己的 m_BufferID → native handle`。

但核查这 4 处的实际形态后，它们**不构成 ownership 冲突**：全部是 `buffer` 与 `source` 同为局部/成员 `shared_ptr` 的一次性流式播放（`MarioDemoApp.cpp:248` 即 `m_BgmSource->Play(m_BgmBuffer)`），PCM 上传后即随 `AudioData` 析构释放，**不保留 PCM、不长期持有 buffer**。

因此它们实际上**已经符合** §1.3 的目标形态，可作为迁移的参照样板。真正需要处理的是把 `AudioClip` 路径对齐到同一形态，而非把 sandbox 迁回 `AudioClip`。

### 1.3 六项子问题的冻结答案

**谁创建 buffer**
`AudioClip`。但**必须通过注入的 `IAudioEngine&` 调用 `CreateBuffer()`**，不得再自行 `alGenBuffers`。`AudioClip` 当前**没有 engine 引用**，需新增注入（见 §1.5）。

**谁拥有 buffer**
`AudioClip` 持有 `shared_ptr<IAudioBuffer>`。播放方（`IAudioSource::Play`）按 `IAudioSource::Play(shared_ptr<IAudioBuffer>)` 的既有契约取得**共享所有权**（`shared_ptr` 传值 → 计数 +1），因此 clip 卸载不会导致播放中 buffer 失效。这是现有接口**已经提供**的 lifetime retention，无需新增机制。

**`IsValid()` 的新语义**
```
IsValid() == m_Buffer != nullptr && m_Info.dataSize > 0 && IsLoaded()
```
即把当前的 `m_BufferID != 0` 替换为 `m_Buffer != nullptr`。**外部可观察语义不变** —— 调用方仍只看到 bool。

**`Release()` / destruction 与 OpenAL context 的顺序**
这是**当前最危险的一处**，必须冻结清楚：

- `OpenALAudioBuffer::~OpenALAudioBuffer()` **无 context 检查**，直接 `alDeleteBuffers`。
- `AudioClip::Release()`（`AudioClip.cpp:144-156`）**有** `HasALContext()` 检查，无 context 时跳过删除。
- `AudioClip::LoadPCM` 同样有 `HasALContext()` 前置检查。

**冻结要求**：迁到 `IAudioBuffer` 后，`AudioClip::Release()` 必须**先释放 `shared_ptr`**，而 `OpenALAudioBuffer` 的析构需要 context 仍存在。因此需要满足二者之一，且**只能选其一**：

- (a) 保证 `AudioClip` 的销毁/卸载**不晚于** `IAudioEngine::Shutdown()`（engine 先于 clip 释放）；或
- (b) 给 `OpenALAudioBuffer` 析构补 context 检查，退化为 `AudioClip::Release()` 现有行为。

**倾向 (b)**：(a) 依赖全局销毁顺序，属于隐式约束；(b) 是局部、可测的，且与已被验证的 `Release()` 行为一致。**此项列为 design freeze 后实施前的唯一待定实现细节**，不改变 ownership 决策本身。

**是否保留 PCM**
**不长期保留。** `AudioLoader::AudioData::pcmData`（`std::vector<uint8>`）在解码后即上传，上传完成即可释放。`AudioClip` 只保留 `AudioClipInfo` 元数据。

两条依据：

1. `AudioClip` 是 `Resource` 子类，走 `ResourceManager` 缓存；同时保留 PCM 会让常驻内存翻倍。
2. **现状本就不保留 PCM** —— `EstimatedMemoryBytes()`（`AudioClip.h:103`）只返回 `m_Info.dataSize`，未计入任何 PCM 占用；`LoadPCM`（`AudioClip.cpp:100-142`）上传后即返回，不缓存 `audioData`。因此"不保留 PCM"是**维持现状**，不是新增约束；迁移若改为保留 PCM 反而是功能倒退。

**如何避免迁移期间出现第二条 provenance path**
硬性顺序约束：

1. `AudioClip` 一次性从 `alGenBuffers` 切换到 `IAudioEngine::CreateBuffer`，**同一次改动内**完成，不允许"先加 `IAudioBuffer` 成员再删 `m_BufferID`"这种两阶段形态。
2. 切换后 `m_BufferID` 若保留，必须是 `m_Buffer->GetNativeHandle()` 的**只读派生**，不得有任何写入路径。
3. 禁止新增任何"第二个 buffer 来源"作为桥接。

**已存在路径的定性**：sandbox 的 4 处直接 `CreateBuffer` 使用（§1.2）虽绕开 `AudioClip`，但已是 `shared_ptr` 持有 + 上传后释放 PCM 的目标形态，**不构成第二条 provenance path**，无需迁移。它们是本节的参照样板。

---

## 2. 冻结结论二：`AudioSourceComponent` 的 engine context

### 2.1 决策

> **`AudioSourceComponent` 在构造期接收 `IAudioEngine&`，不引入 singleton、不使用全局状态。**

### 2.2 依据：同 codebase 已有可工作先例

`Audio::PlayOneShot(IAudioEngine& engine, AudioClip& clip, const Vec3& position)`（`AudioSystem.cpp:30`）**已经以 engine 引用为参数**完成了完整流程：创建 source → 绑定 buffer → 播放 → 回收。该函数有 14 处引用，横跨 `AudioEngine.cpp`、4 个 sandbox。

**因此"engine 必须靠 singleton 才能拿到"这一顾虑不成立** —— 现有代码已经证明引用传递是可行形态。

### 2.3 冻结的形态

```
AudioSourceComponent(IAudioEngine& engine, std::shared_ptr<IAudioSource> source)
```

- 现有构造 `AudioSourceComponent(std::shared_ptr<IAudioSource>)`（`AudioSourceComponent.h:43`）**保留为兼容重载**，但标记为过渡期 API；它在没有 engine 的情况下无法完成 `AudioClip → IAudioBuffer` 转换，故只能用于不播放外部 clip 的场景。
- `engine` 以**引用**持有（`IAudioEngine& m_Engine`），生命周期由调用方保证，与 `PlayOneShot` 一致。
- **不**引入 `GetInstance()`、函数内 static、或其他隐藏全局。

### 2.4 目标唯一播放链

```
AudioClip.m_Buffer (shared_ptr<IAudioBuffer>)
    → IAudioSource::Play(buffer)
```

`IAudioSource::Play` 已封装 buffer 绑定、播放、错误检查与 lifetime retention。当前 `AudioSourceComponent::Play`（`AudioSourceComponent.cpp:38-54`）手工 `alSourcei` + `m_Source->Play(nullptr)`，迁移后应变为单次 `m_Source->Play(buffer)`。

`Play(nullptr)` 这一现行 no-op 调用（`AudioSourceComponent.cpp:54`）将随之消失。

---

## 3. 冻结结论三：bypass 收敛边界

27 处需处置的 native 调用分类如下（另 13 处 canonical backend 保留，见 §3.5）。

### 3.1 Stack 2 consumer bypass → 必须迁移（8 处 / 2 文件）

| 文件 | 处数 | 现状 | 目标 |
|---|---|---|---|
`AudioSystem.cpp` | 5 | `alSourcei` + `alSourcePlay`，绕过已持有的 `IAudioSource&` | 改用 `source->Play(clip.GetBuffer())` |
`AudioSourceComponent.cpp` | 3 | 同上 + `Play(nullptr)` no-op | 改用 `m_Source->Play(buffer)` |

这两处是**最高优先级**：它们已经持有 Stack 2 接口对象，却仍手工操作 native handle，纯属冗余。

### 3.2 Stack 1 → 最终退休（12 处 / 2 文件）

| 文件 | 处数 | 说明 |
|---|---|---|
`AudioSource.cpp` | 10 | Stack 1 直接 OpenAL；`AudioSource` 自持 `m_SourceID` + `m_Context`（`AudioSource.h:97-98`） |
`AudioEngine.cpp` | 2 | 仅 `alGetError` 设备查询 |

`AudioSource` 的 feature 面**远超** Stack 2 的 `IAudioSource`（`SetOcclusion` / `ApplySpatialIR` / aux sends / cone 等，`AudioSource.h:60-84`）。**这是 Stack 1 退休的真实成本所在**，须在 Phase C 之前确认这些能力是否需要进入 Stack 2，否则退休会造成功能回退。

### 3.3 `AudioClip` 自持 native buffer（5 处 → 随 §1 一并消失）

`AudioClip.cpp` 的 5 处（`alGenBuffers` / `alBufferData` / `alDeleteBuffers` / `alGetError`）在 §1 迁移后**全部消失**，由 `OpenALAudioBuffer` 承担。

### 3.4 文档注释 → 立即修正（2 处）

`AudioClip.h:21-24` 的使用示例直接示范：

```cpp
if (clip && clip->IsValid()) {
    alSourcei(source, AL_BUFFER, clip->GetBufferHandle());
    alSourcePlay(source);
}
```

**列为接受 Stack 2 后的第一项低风险 cleanup**，与 §1/§2 的代码迁移**分开提交**。理由：它零风险、能立即停止引导新代码复制绕过写法，且不依赖 ownership 结论。**本次 design freeze 阶段不修改。**

### 3.5 canonical backend → 必须保留（13 处 / 3 文件）

| 文件 | 处数 |
|---|---|
`OpenALAudioSource.cpp` | 9 |
`OpenALAudioBuffer.cpp` | 3 |
`OpenALAudioSource.h` | 1 |

这些是 `IAudioSource` / `IAudioBuffer` 的实现，native 调用是其职责所在，**不属于 bypass**。

---

## 4. 迁移链（唯一 buffer 来源 → 唯一 owner → 唯一播放入口 → Stack 1 退休条件）

```
[唯一 buffer 来源]
  AudioLoader 解码 → AudioData.pcmData（中间态）
        ↓ CreateBuffer(pcm, size, info)
  IAudioEngine::CreateBuffer() → shared_ptr<IAudioBuffer>
        ↓ AudioClip 持有
[唯一 owner]
  AudioClip::m_Buffer : shared_ptr<IAudioBuffer>
  （m_BufferID 降级为只读派生视图，不再是 owner）
        ↓ shared_ptr 传值共享所有权
[唯一播放入口]
  IAudioSource::Play(shared_ptr<IAudioBuffer>)
  实现方：PlayOneShot(engine, clip, pos)
          AudioSourceComponent::Play(clip)   ← 构造期注入 engine
        ↓
[Stack 1 retirement 条件]
  全部满足方可退休 Stack 1：
   1. §3.1 的 8 处 consumer bypass 全部迁移完毕
   2. §1 的 AudioClip 迁移完毕（无第二个 buffer provenance path）
   3. Stack 1 独有能力已确认归属（§3.2 的功能缺口已补齐或明确放弃）
   4. 现有 112 个 headless audio tests 归属已确定
```

**唯一 playback entry point 收敛后，抽象层播放路径只有 `IAudioSource::Play`。**

---

## 5. 迁移完成前继续保持 compatibility contract 的 API

以下在迁移完成前**不得删除或改变语义**：

| API | 位置 | 理由 |
|---|---|---|
`AudioClip::GetBufferHandle()` | `AudioClip.h:80` | 多个 native consumer 在用；且 `AudioClip.h:22` 注释示范它 |
`AudioClip::m_BufferID` | `AudioClip.h:135` | 参与 `IsValid()`；Phase D 前保持 |
`AudioClip::Release()` | `AudioClip.h:129` | 现有 context 检查语义是迁移的参照基线 |
`AudioSource::SetClip` / `GetClip` | `AudioSource.h:39-40` | Stack 1 仍持有 `AudioClip*` |
`AudioSourceComponent(shared_ptr<IAudioSource>)` | `AudioSourceComponent.h:43` | 过渡期兼容重载 |
`IAudioSource::GetNativeHandle()` | `IAudioSource.h` | backend 实现需要；**接口保留，但生产调用方须清零** |

---

## 6. Phase A 前置决策：`IAudioEngine` 的注入边界（Architecture Review 追加冻结）

> 本节为架构评审追加冻结， supersedes 原 §6。
> **Phase A 已解锁**：注入边界与 engine provisioning 均已冻结（§6.2 / §6.6b）。

### 6.1 原冻结遗漏的层级

§1 的创建链写作：

```
AudioLoader → PCM → injected IAudioEngine::CreateBuffer() → AudioClip.m_Buffer
```

实施前的只读追查证明，这条链**缺少一层前置契约**。`AudioClip` 是 `Resource` 子类，生产创建路径为：

```
ResourceManager::Load<AudioClip>(path)          ResourceManager.h:108
  → LoadByType<AudioClip>(path)                 ResourceManager.cpp:117
      → make_shared<AudioClip>(path)            ← 无 engine 参数
      → clip->LoadFromFile(path)                ← 内部直接 alGenBuffers
```

`ResourceManager` **不持有任何音频依赖**：`Init(IGraphicsFactory&)`（`ResourceManager.h:89`）是唯一初始化入口，且**全仓唯一调用点是 `Application.cpp:76`** —— 属 HRC-3 未提交改动（`+338/−12`）。

因此 §1 写下的"injected `IAudioEngine`"在当前结构下**没有可注入的位置**。这是原冻结遗漏的**架构级 ownership / dependency-boundary 决策**，不是实现细节。

### 6.2 冻结结论

> **`AudioClip` 的 `IAudioEngine` 依赖必须在 `ResourceManager` 的资源创建边界显式提供。**
>
> **不得**通过 `AudioClip` setter 的隐式时序注入。
> **不得**让 Resource 层直接实例化 OpenAL backend。

### 6.3 三个候选方案的处置

| 方案 | 处置 | 理由 |
|---|---|---|
**A. `ResourceManager` 显式注入 `IAudioEngine&`** | **倾向的长期方向** | `ResourceManager` 本就是资源创建边界；显式依赖比隐式 setter 更易形成确定契约。**当前不实施** |
**B. `AudioClip::SetAudioEngine()` + load 前注入** | **拒绝** | 把"可独立加载的 Resource"变成"必须先满足隐式时序才能加载的 Resource"；全仓 17 处 `AudioClip` 创建/加载点极易漏注入 |
**C. `AudioClip` 内部直接构造 `OpenALAudioBuffer`** | **拒绝** | 会让 Resource 层直接知道 OpenAL backend，**反向破坏已接受的 Stack 2 canonical 决策** |

### 6.4 只读 boundary audit 结论（**已更正**）

> **更正**：本节初版结论为"不存在已提交的注入入口"。该表述在**签名层面正确，但在结构层面过强** —— 它把"没有 engine 可传"误述为"没有入口可传"。

**注入边界本身已存在于已提交代码中**：`ResourceManager::Init(m_Factory)` 是 `Application` 注册的一个 `SubsystemManager` 条目，位于 `SubsystemPhase::Resources`(4)，HEAD `Application.cpp:53-59`：

```cpp
m_SubsystemManager.Add(
    "ResourceManager", SubsystemPhase::Resources,
    [this]() {
      ResourceManager::Init(m_Factory);
      return ResourceManager::Get() != nullptr;
    },
    []() { ResourceManager::Shutdown(); });
```

因此：

| 事实 | 结论 |
|---|---|
`ResourceManager` 注入边界是否需要 HRC-3 才出现 | **不需要**。已存在于 committed `Application` subsystem registration |
`ResourceManager::Init` 是否有第二/第三调用点 | 无，仅此一处（HEAD `Application.cpp:56`） |
`ResourceManager` 是否持有可复用的注入 setter | 无。现有接口仅 `Init` / `Shutdown` / `Get` / `SetBudget` / `GetFileWatcher` |
`ResourceManager::Init` 是否还缺其它外部依赖 | **无**。核查其全部行为（`ResourceManager.cpp:24-38`）：构造单例、设预算、`InitPools()`、打日志 |

**真正未决的是 engine provisioning**：committed 代码中没有任何 `IAudioEngine` owner，且 `Resources`(4) 之前没有任何 subsystem 创建音频 engine。已提交的 `OpenALAudioEngine` 构造点**全部在 sandbox**（`AudioTestApp.cpp:27`、`AudioPhysicsSandboxApp.cpp:378`、`CollisionAudioTestApp.cpp:167`、`MarioDemoApp.cpp:29`），形态一致：`shared_ptr` 持有 + `Init()` 失败即中止 —— 这是现成的 owner 模式参照。

`Application.h:17` 的 `make_shared<OpenALAudioEngine>()` 是 `AudioSourceComponent` 的**文档注释**，非代码。

### 6.5 Phase A 解锁：三项 provisioning 决定（架构评审冻结）

#### 决定一：`IAudioEngine` owner 置于 **Platform phase**

不置于 `Core`，不新增 phase taxonomy。

依据：`SubsystemManager::Initialize()` 以 `std::stable_sort` 按 phase 升序（`SubsystemManager.cpp:44-47`），且同 phase 内**按注册顺序**执行（stable）。已验证 phase 顺序 `Platform(1) → Graphics(2) → Input(3) → Resources(4)`。

**实施约束（必须显式保证，不得依赖当前巧合）**：

> **`Window` 必须先于 Audio engine 注册。**

OpenAL 需要 device/context，而 `Window` 在同一 `Platform` phase 创建（HEAD `Application.cpp:42`）。只有 `Window` 注册在前，Audio engine 才能在 context 就绪后构造。该顺序必须写成显式约束并有测试或注释固定，**不得成为"碰巧现在先后如此"**。

#### 决定二：engine 存放在 **`Application` member**

```cpp
Application
  └─ Platform phase（Window 之后）
       └─ 创建并持有 IAudioEngine
             ↓
  Resources phase
       └─ ResourceManager::Init(m_Factory, *m_AudioEngine)
```

| 约束 | 说明 |
|---|---|
不做 subsystem singleton | 不引入新的全局状态 |
`ResourceManager` 不自行寻找 engine | 它只依赖 `IAudioEngine&`，**不知道 OpenAL** |
sandbox 保留自建 engine | 4 个 sandbox 现有模式不受影响 |
HRC headless 不需为 Audio 初始化 `ResourceManager` | 保持现状 |
`Shutdown` 由 Platform subsystem 的 `onShutdown` 释放 | **不依赖 `Application` 析构顺序碰巧成立** |

成员位置参照现有模式：`m_Factory` 已是 `IGraphicsFactory&` 成员（`Application.h:243`），且有 `GetFactory()` 访问器（`:125`）。

#### 决定三：**扩展现有** test-local `FakeAudioEngine`

不新建第二个 fake，不引入 production mock。

`test_audio/ListenerTest.cpp:49` 已有 `class FakeAudioEngine : public IAudioEngine`，注释明确"无 OpenAL / 无音频设备"。扩展其 `CreateBuffer` 为返回 `FakeAudioBuffer`（实现 `IAudioBuffer`），并记录传入的 `dataSize` / `AudioClipInfo`，使 Phase A 可在**无 OpenAL、无 HRC headless、无真实 device** 的条件下验证：

```
AudioLoader → IAudioEngine::CreateBuffer() → AudioClip.m_Buffer
```

**测试边界**：`FakeAudioEngine` / `FakeAudioBuffer` 只服务 `test_audio`，**不进入 EngineCore**。

**现状缺口**：`FakeAudioEngine::CreateBuffer` 当前直接返回 `nullptr`（`ListenerTest.cpp:55-57`），不足以验证 ownership。另经核查，**`AudioClip` 目前零直接测试覆盖** —— 全 tests 目录非注释的 `AudioClip` 行仅 2 处，且都是回调签名与 `AudioClipInfo` 参数，无任何测试构造或加载 `AudioClip`。

### 6.6 Phase A 最终结构

```
Application
  └─ Platform phase（Window 之后）
       ├─ Window
       └─ IAudioEngine owner
             ↓
  Resources phase
       └─ ResourceManager::Init(factory, audioEngine)
             ↓
       AudioClip creation
             ↓
       AudioLoader → PCM → CreateBuffer()
             ↓
       AudioClip::m_Buffer
             ↓
       IAudioSource::Play(shared_ptr)
```

**HRC headless 保持不变**：

```
InitializeHeadless()
    └─ skip ResourceManager
```

Phase A 的 ownership 测试走**独立的 headless unit setup**，不依赖 HRC headless bootstrap。

### 6.7 Phase A 实施边界

| 项 | 内容 |
|---|---|
第一个 production commit | `AudioClip m_BufferID → shared_ptr<IAudioBuffer>`；`ResourceManager` 显式获得 `IAudioEngine&` |
**不夹带** | Phase B 的 8 处 bypass 迁移；Stack 1 退休；`AudioClip.h` 注释 cleanup；null-driver policy |
提交划分 | production ownership migration → tests/cleanup → 三配置 full build + full suite |
**不做偷渡项目** | `Core`/`core` `Application.cpp` case collision 属独立 repo-integrity decision |

### 6.8 两项待纠正的记录（不在本次处理）

| 项 | 状态 |
|---|---|
`16/16` 基线口径 | 实为 **13 committed + 3 HRC = 16/16**；纯 HEAD 为 **13/13**。标记为 evidence correction required，待 HRC 收口文档统一更正 |
`Core`/`core` `Application.cpp` case collision | 标记为 HRC landing prerequisite / 独立 repo-integrity decision，暂不修 |

### 6.9 不受影响的部分

| 项 | 状态 |
|---|---|
`8f15827`（`OpenALAudioBuffer` destructor context guard） | **保持已关闭，不回滚** |
§1.3 context 释放顺序 | 已由 `8f15827` 落地（原倾向 (b)） |
`AudioClip` 当前 `m_BufferID` compatibility contract | **保持不变**，直到注入边界确定 |
§5 全部 compatibility API | 保持 |
§3 bypass 分类与 `AudioClip.h` 注释 cleanup | 保持为独立低风险 cleanup |

---

## 7. 本次审计不做的事

- 不修改 HRC-3 工作区
- 不重新打开 B4（已关闭）
- 不把 OpenAL null-driver / headless policy 混入本设计（独立的 backend/CI 决策）
- 不为纠正 §6.8 的基线口径而改动 HRC 文档
- 不把 `Core`/`core` case collision 作为 Phase A 的偷渡项目
- 不在 Phase A 中夹带 Phase B 的 bypass 迁移

> 本文档为设计记录。Phase A 的代码实施由 §6.7 的提交边界约束，单独进行。