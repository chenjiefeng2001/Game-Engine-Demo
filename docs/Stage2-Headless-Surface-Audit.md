# 阶段 2：headless 面盘点审计记录

> **状态**: 已完成（Complete）
> **最后更新**: 2026-10-04
> **范围**: Core/Memory、Platform utils (`Time`)、Core/Resources、Core/SceneManager、Scripting、Core/Physics
> **明确跳过**: Core/ECS（等待 B4 ADR / 产品意图）
> **关联**: `docs/ADR-B4-ECS-Registration.md`、`docs/ADR-Audio-Canonical-Stack.md`

---

## 0. 本次审计的定位

阶段 2 是**发现审计**，不是设计阶段。输出只有四类信息：依赖准入分类、现有覆盖、缺陷/风险（区分 active / latent / observation）、以及**仅对 Category 1** 给出的最小候选测试面。

**本次未写任何测试、未改任何 CMake、未引入任何 mock、未做任何生产代码修改。**

---

## 1. 方法学更正（重要，优先于结论）

盘点过程中我曾得出一个**错误结论**："Core/Memory 零测试覆盖"。

实际情况是 `tests/test_core/memory/StackAllocatorTest.cpp` 早已存在，含 8 个真实行为测试并已注册进 `test_core`。

**根因是我自己的检索 bug**：PowerShell 的 `-notmatch` **大小写不敏感**。我用于排除 `Core/Memory` 自身的过滤正则 `Core\Memory`，同时匹配上了测试路径 `test_core\memory`，把要找的测试文件一并过滤掉。

**修正方式**：全部统计改用 `-CaseSensitive` 重跑。

**为什么保留这条记录**：

> 检索误差本身不是代码缺陷，但它直接影响结论可信度。这类"误差被发现 → 重新验证 → 结论修正"的证据链，比最初那个错误数字更值得留存 —— 它说明本审计的分类结论是在**复核后**成立的，而非一次扫描的产物。

**对后续审计的约束**：任何跨目录检索若使用路径排除，必须使用大小写敏感匹配，或先验证排除规则不会命中目标目录。

---

## 2. 盘点结论

| 子系统 | 分类 | 现有覆盖 | 处置 |
|---|---|---|---|
Core/Memory | 1 | **8 个真实行为测试** | **不推进** |
Platform utils (`Time`) | 1 | **0** | **Stage 3 ready（实现门控）** |
Core/Resources | 2 | 3 处引用 | 暂缓 |
Core/SceneManager | 1（倾向） | 0 | 暂缓（边界未定义） |
Scripting | 1 | 127 处引用 | 不推进 |
Core/Physics | 3（部分） | 54 tests | 暂缓（Device 边界） |
Core/ECS | — | — | 继续跳过（等待 B4 决策） |

---

## 3. Core/Memory —— 不推进

**准入**：Category 1。依赖闭包 = `Types.h` + `Log.h` + `<cstdlib>`；无设备、无 singleton、无文件系统。

**现有覆盖**：`AlignedAllocation`、`MultipleAllocations`、`MarkerRollback`、`UsedBytesTracking`、`OverAllocationReturnsNull`、`AllocateZeroBytes`、`CapacityAndRemaining`、`ResetReusesMemory` —— 属真实行为覆盖，非 smoke。

**观察**：
- `Core/Containers/StackList.h` 零消费者 → 死表面
- `NewOnStack<T>` / `NewArrayOnStack<T>` / `DumpState()` / `StackAllocatorAdaptor` 未被测试引用（后者在 `OpenGLGraphicsFactory.cpp` 有 9 处生产使用）

**曾疑似但经核实不是缺陷**：
- alignment 非 2 的幂 / alignment=0 / `FreeTo` 不调析构 → 全部是**已文档化的前置条件**（`StackAllocator.h:40` "必须是 2 的幂"、`:11` "不适用于需要正确调用析构函数的组件"）
- `g_SubsystemAllocator` 疑似未赋值 → 实际在 `Application.cpp:127/848` 正确赋值、`:116/:158` 清零，`CrashHandler:257` 有 null 保护

**结论**：残余缺口不足以单独立项。

---

## 4. Platform utils (`Time`) —— Stage 3 ready，但实现门控

**准入**：Category 1。闭包 = `<cstdint>` + `<chrono>` + `<cmath>`。无设备依赖。
**测试约束**：全部状态为 static 全局量 → 测试须串行，每例前后 `Init()` / `Shutdown()`。

**现有覆盖**：**0**（`Time::` / `Engine::Time` / `CalibrateAccumulator` 在 tests/ 下均 0 引用）。

### 门控状态（已解除）

> **Time：Stage 3 可立即开始。HRC 当前未提交改动未改变既有计算语义。`Shutdown()` / `IsInitialized()` 属于未提交的新增生命周期 API；若 HRC 后续改变其接口，仅对应测试用例可能需要迁移。**

**门控判据的修正**：原门控写的是"等待 `Time.cpp` 稳定"，隐含把**未提交**等同于**语义不稳定**。逐项核对后该等同不成立 —— HRC 对 `Time.cpp` 的 10 行是纯增量，只新增 `Shutdown()` 与 `IsInitialized()` 两个生命周期 API，**未触碰** `Init()`、`GetTimeD()` 惰性自初始化、`UpdateDeltaTime()` 钳制、`SetTimeScale()` 钳制、`CalibrateAccumulator()` 语义、`GetTickFrequency()`。

因此原先假设的"当前代码 → 测试落地 → HRC 改动 → 测试再次迁移"链路对这批语义并不成立，门控改由**语义是否改变**判定，而非提交状态。

**残留风险的准确范围**：仅 `Shutdown()` / `IsInitialized()` 两个用例可能因 HRC 改动其接口而需要迁移；其余用例不受影响。

**附带事实**（记录归属用）：`Time::Shutdown()` 的唯一消费者 `Application::Shutdown()` 在 HEAD 中并不存在，属 HRC-3 新方法（嵌于 `Application.cpp` 12 个 hunk 中的第 4 个）；`Time::IsInitialized()` 的唯一消费者 `bridge/src/EngineHost.cpp` 为 untracked 文件。两个新 API 目前只服务于 HRC-3 的生命周期工作。

### 既定四步

`behavioral surface → 独立 fix → assertion migration → 三配置全量回归`

### 已接受的候选行为面

- `Init()` / `Shutdown()` 状态机
- `GetTimeD()` 的惰性自初始化
- `SetTimeScale()` 负值钳制到 0
- `GetGameDeltaTime() == GetDeltaTime() × TimeScale`
- `UpdateDeltaTime()` 的 maxDt 钳制与负值归零
- `GetTickFrequency() > 0`
- **`CalibrateAccumulator()` 的当前语义**：`accumulator` 参数被忽略、返回 `fmod(GetElapsedSinceInit(), fixedDt)`

### 观察（非缺陷，测试须按现状固化）

- `CalibrateAccumulator` 丢弃传入 `accumulator`（`Time.cpp:152 (void)accumulator;`），改用自 Init 起的总墙钟时间。**已文档化**（`:153-154`）。唯一生产调用点 `Application.cpp:964`，每 600 帧一次。
  → **`accumulator` 被忽略是现有定义，不得因为看起来奇怪而偷偷"修正"。**
- `fixedDt == 0` 时 `std::fmod(x, 0.0)` 为 UB。唯一调用点使用 `1.0f/60.0f` 常量 → **latent，不可达**
- `Application.cpp:959/962` 以函数内 `static` 保存 `acc` / `calibrateCounter`，跨 Application 实例残留

---

## 5. 暂缓项的最小边界记录

**Core/SceneManager**：
> 倾向 Category 1，但其 LevelManager 成员及 LoadLevel/Update/async 路径尚未划分为纯 CPU 面与运行期耦合面，因此暂不创建候选测试面。

**Core/Physics**：
> CPU/Box2D 面已有 54 tests；其余代码存在真实 GL/RHI device boundary，当前未建立可接受的 headless 分界，因此不进入 Stage 3。

**Core/Resources**：Category 2（`ResourceManager::Get()` 单例 + 磁盘路径，`AudioClip` 创建路径位于其中）。

以上三项**不再为"以后方便"补做额外审计**。未来重新进入时，上表即为"为什么没继续"的依据。

---

## 6. 本次明确未做的事

- 未写任何测试、未改 CMake、未引入 mock
- 未做任何生产代码修改
- 未对 SceneManager / Core/Physics 追加中等规模审计
- 未重开 ECS
- 未触碰 HRC-3 的 4 个未暂存 hunk

---

## 7. 阶段 3 的启动条件

> **等待 HRC `Time.cpp` 稳定 → 直接进入 `Time` 的 Stage 3。**

在此之前不启动任何新的阶段 3 工作。