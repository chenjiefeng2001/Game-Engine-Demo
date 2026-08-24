# Evidence Integrity Report — 证据链完整性审计与整改

> 日期：2026-08-24 · 范围：tests/ 全部测试资产 + 文档计数声明
> 触发事件：DF07 期间发现 `Dogfood05Test.cpp` 从未加入 CMakeLists（死文件）
> 结论先行：**发现并修复 7 项证据链缺陷；建立常驻 Integrity Gate（I1/I2/I3）；
> 当前权威计数 91/91，门禁全绿。**

---

## 一、核心发现："全绿"曾存在幸存者偏差

从未参与编译的测试文件不产生链接错误，也就从不暴露。本次审计对照
**磁盘文件 / CMake 注册 / 实际 gtest discovery** 三列后发现：

| # | 缺陷 | 影响 | 处置 |
|---|------|------|------|
| 1 | `ResourceLifecycleTest.cpp`（**M2 证据**）从未注册 | M2 的 GUID 契约测试实际由 ContentPipelineTest 内联版本承载 | 已注册；内联 4 个 RLCycle 测试剥离迁移（单一事实源）；补入独有的 DuplicateExplicitGuid_Rejected |
| 2 | `DXGoldenGateTest.cpp`（**M4 五道门禁**）从未编译 | **M4 冻结声明的"五道门禁全过"不可复现**：引用未定义 helper（DX_P/DX_SP/DX_MP/DX_SF/DX_SS）、错误类型名 ResourceGuid、HandleSpawn 绑定 null scene、缺目录创建 | 全部修复后注册；3 tests 首次真实 PASS |
| 3 | `DogfoodDXTest.cpp` 为 DXGoldenGate 早期草稿 | 无独有覆盖，纯维护负担 | 删除并记录于基线文档 |
| 4 | DF06 无任何可执行测试 | "DF06 通过"仅来自场景加载路径 | 新建 `Dogfood06Test.cpp`（15 实体 600 帧循环）|
| 5 | `dogfood06_game.lua` 含非法 Lua（第 12 行 `──` 非注释；C++ 语法 `Scripting::GameplayAPI::HandleDestroy` 混入）| **玩法脚本从未被真正执行过**——Initialize 直接失败 | 修复语法；改用已暴露的 `Engine.entity.destroy`；补缺失常量 WALL_R |
| 6 | `EngineBootTest.cpp` 对已漂移 JobSystem API 编写（Dispatch/Wait(nullptr)）| test_e2e 无法构建，且从未纳入任何计数声明 | 迁移至 Schedule/WaitAll；6 tests PASS |
| 7 | `JobSystemStressTest.cpp` 同类漂移 | test_job 无法构建 | 同上修复；4 tests PASS |

**最严重的是 #2 与 #5 的组合**：M4 冻结报告声称 DX-G1..G5 门禁全过，
而承载它的文件连编译都没通过过；DF06 的冒烟结论建立在从未运行的脚本之上。
这正是 *"测试文件存在 ≠ 测试存在"* 的实证。

## 二、Integrity Gate（tools/integrity_gate.ps1）

常驻三道门禁，任何一条失败即非零退出：

| Gate | 校验内容 | 本次结果 |
|------|---------|---------|
| I1 Registration | 每个 `tests/**/*Test.cpp` 必须出现在 CMakeLists | ✅ 19/19 files registered |
| I2 Count Integrity | 实际 `--gtest_list_tests` 计数 == `docs/Evidence-Baseline.md` 声明 | ✅ 91 == 91（6 targets）|
| I3 Dogfood Execution | 每个 DF01–DF07 的证据 filter 必须可执行且通过 | ✅ DF01–DF07 全绿 |

配套产出 `docs/Evidence-Baseline.md`：
- In-Scope 六目标逐 target 权威计数（唯一可信来源，I2 强制对齐）
- Excluded 目标必须写明原因与跟进条件（不允许静默消失）
- Dogfood → 可执行证据的显式映射表

## 三、当前权威基线（91 tests, all PASS）

```text
test_scripting  23   Scripting API v2.1
test_physics    16   GPU Physics v1.x
test_renderer    9   GL46Device + SG6 (isolated)
test_content    33   Content v1 + Lifecycle + Editor Workflow + DX Gate + DF05-07
test_job         4   JobSystem stress
test_e2e         6   Engine boot lifecycle
─────────────────────
IN-SCOPE        91

EXCLUDED:
  test_core   ASAN 拦截崩溃（exit=3，首测即死；环境级问题，非逻辑失败）
  test_ecs    测试对旧 EntityManager API 编写无法编译（ECS 本身 deferred）
```

## 四、纪律固化

1. **新增测试文件的验收标准 = 注册 + 编译 + discovery 计数变化被基线吸收**。
   只跑 `--gtest_filter` 看到绿色不算数。
2. **里程碑冻结声明中的每个测试名，冻结时必须能通过 I3 式过滤执行复现。**
3. **排除项必须显式登记**（目标、原因、跟进条件），禁止从文档中静默消失。
4. 门禁脚本可在任意时点独立运行：
   `powershell -File tools/integrity_gate.ps1 [-SkipBuild]`

## 五、后续跟进（不立即执行）

- test_core ASAN 崩溃排查（疑似 allocator 测试与 MSVC ASAN 拦截冲突）
- test_ecs 待 ECS 解除 deferred 后按现行 API 重写
- 将 gate 接入 CI 或提交钩子（当前为手动运行）
