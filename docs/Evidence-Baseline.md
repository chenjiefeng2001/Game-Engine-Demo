# Evidence Baseline — 权威测试计数基准

> 建立日期：2026-08-24 · 维护纪律：**本文件中的数字必须与实际 gtest discovery 一致**
> 由 `tools/integrity_gate.ps1` 强制校验（I2 门禁）
>
> 原则：**"测试文件存在" ≠ "测试存在"。只有被构建系统注册、被运行器发现并执行，才算 Evidence。**

---

## In-Scope 目标（门禁强制校验）

| Target | 期望计数 | 状态 | 承载证据 |
|--------|---------|------|---------|
| test_scripting | 25 | VERIFIED | Scripting API v2.1 C-ABI/MVP/Gameplay + F1 通用 `Engine.component.*` + Lua Camera 实证 |
| test_physics | 16 | VERIFIED | GPU Physics v1.x CPU/GPU ring |
| test_renderer | 9 | VERIFIED | GL46Device + RenderGraph SG6 隔离验证 |
| test_content | 42 | VERIFIED | Content Pipeline v1 + Resource Lifecycle + Editor Workflow + DF05/06/07 + DX Golden Gate + VS01 |
| test_job | 4 | VERIFIED | JobSystem 压力 |
| test_e2e | 6 | VERIFIED | 引擎启动生命周期 |
| test_gp01 | 22 | VERIFIED | Game Production GP-P1 · GP01 生产契约（GP1-A/B/C/D + DX 审计 remediation D7） |
| test_bridge | 25 | VERIFIED | EditorBridge C-ABI 契约（P3-B/P3-C：Script/Import/Rename 绑定 + 资产 index 稳定；P3-D：Runtime Play/Reload(_PERSIST)/Stop/错误恢复；AV-G3 E3：缺失 GUID 资产告警 + 实体保留；F1：契约组件 Camera 生命周期/负路径/落盘重启/未知类型告警） |
| test_core | 20 | VERIFIED | GLM 数学库 + StackAllocator（2026-08-28：Vector3Test 默认构造误读未初始化内存 → 改为值初始化后纳入） |
| **合计** | **169** | | |

## Excluded 目标（记录原因，不计入基线）

| Target | 状态 | 原因 | 跟进条件 |
|--------|------|------|---------|
| test_ecs | EXCLUDED | 测试对旧 EntityManager API 编写，无法编译 | ECS 解除 deferred 后重写 |

## Dogfood → 可执行证据映射（I3 门禁强制校验）

每个 Dogfood 的冒烟结论必须能指向一个**已注册且可执行**的 gtest：

| DF | 主题 | 承载测试（target : filter） |
|----|------|---------------------------|
| DF01 | Entity.find + UI.text | test_scripting : `ScriptingGameplayTest.*` ＋ test_content : `GameplayAPIV2.M001_EntityFind_ByName` |
| DF02 | GUID→Resource→Restart 全链 | test_content : `ContentGolden.R13Golden_*` ＋ `DXGoldenGate.R13_Process*` |
| DF03 | 多实体/Timer/Win-Lose | test_content : `ContentDogfood.DF03_*` |
| DF04 | DX Audit（八项摩擦） | test_content : `DXGoldenGate.Full_Workflow_EmptyProject_To_PlayableGame`（DX-G1..G4 内联）|
| DF05 | Arena Survival | test_content : `Dogfood05.LoadAndRun` |
| DF06 | Multi-type Combat | test_content : `Dogfood06.LoadAndRun` |
| DF07 | Tower Defense + Restart round-trip | test_content : `Dogfood07.LoadRunRestart` |
| VS01 | Vertical Slice 01 产品现实门（menu→arena→boss 全流程） | test_content : `VS01.*` ＋ `VS01Debug.MinimalReloadRepro`（F1 回归守卫） |
| GP01 | Game Production Phase 1 生产契约（GP-P1 章程 §11，随阶段逐个点亮） | test_gp01 : `GP01.*` |

## 2026-08-28 整改记录（ASan/CTest 基础设施硬化）

起因：`test_bridge.exe` 直跑（无 `ASAN_WIN_CONTINUE_ON_INTERCEPTION_FAILURE=1`）
在 MSVC ASan interception 层硬失败挂死（`interception_win.cpp:193`），并遗留
冻结进程锁死 `gp01_editor_scratch`。诊断与处置见
`docs/Avalonia-Phase3-Freeze-ASan-Report.md`。本次整改：

1. `tests/CMakeLists.txt`：ASan+MSVC 时对所有测试 target 设 CTest
   `ENVIRONMENT ASAN_WIN_CONTINUE_ON_INTERCEPTION_FAILURE=1` → ctest 直跑永不卡死；
   同时统一 `WORKING_DIRECTORY = 仓库根`（此前 ctest 在 build/tests 下运行，
   相对资产路径全部落空 → 假失败）。
2. `tools/run_test.cmd`：直跑 ASan 测试 exe 的统一启动器（main() 内设置 env
   已太晚，必须在进程启动前生效）。
3. test_core 从 EXCLUDED 恢复：exit=3 首测即死实为 `vec3 v;` 未初始化读
   （注释错误宣称 GLM 会零初始化）→ 改为 `vec3 v{}`，20/20 PASS。
4. test_bridge 首次纳入基线（16 用例，含 P3-B/P3-C 契约；P3-C 收官时
   新增会话内资产 index 稳定契约测试 → 17 用例）。
5. 合计 122 → 159。

**长期纪律**：Windows/MSVC ASan 测试必须经统一 launcher / CTest environment
启动；"裸跑 test exe 无 env" 不作为可靠证据。

---

## 2026-08-24 整改记录（Evidence Integrity Gate 建立时发现）

以下问题由本次审计暴露并当场修复：

1. `ResourceLifecycleTest.cpp`（M2 证据文件）从未注册 → 已注册；
   ContentPipelineTest.cpp 中 4 个内联 RLCycle 测试剥离至该文件（单一事实源）。
2. `DXGoldenGateTest.cpp`（M4 五道门禁）从未编译——引用未定义 helper（DX_P 等）、
   错误类型名（ResourceGuid）、HandleSpawn 绑定 null scene → 修复后注册，
   **M4 冻结声明首次获得可复现证据**（3 tests PASS）。
3. `DogfoodDXTest.cpp` 为 DXGoldenGate 的早期草稿，从未编译且无独有覆盖 → 删除并记录于此。
4. DF06 无任何可执行测试 → 新建 `Dogfood06Test.cpp`（15 实体 600 帧）。
5. `dogfood06_game.lua` 含非法 Lua 语法（第 12 行 `──` 非 `--` 注释；C++ 语法混入）→ 修复。
   该脚本在 DF06 当时的"通过"实际只来自场景加载路径，玩法脚本从未被真正执行过。
6. `EngineBootTest.cpp` / `JobSystemStressTest.cpp` 对已漂移 JobSystem API 编写
   （Dispatch→Schedule、Wait(nullptr)→WaitAll）→ 修复，两目标恢复构建并通过。
7. test_e2e 此前从未被纳入任何计数声明。

**教训**：DF01–DF04 时代的"全绿"存在幸存者偏差——从未参与编译的文件不产生链接错误，
也就从不暴露。Integrity Gate 即为此而设。
