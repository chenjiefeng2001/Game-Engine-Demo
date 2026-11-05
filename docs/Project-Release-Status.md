# Project-Release-Status.md — 当前项目状态（单一事实入口）

> 本文件只反映**当前最新**状态；历史阶段记录保持不可变。
> 更新规则：每完成一个 Work Package 或状态跃迁时重写本页。
> 缺口明细见 `docs/Project-Gap-Register.md`。

| 字段 | 值 |
|---|---|
| **Current Branch** | `avalonia` |
| **Authoring HEAD** | `a9daee4`（P5-F3-U1 收口；P6-0/P6-1A 文档提交见下方 Delta） |
| **Remote** | origin/avalonia **0/0 同步**（P6-0 Gate 0A 已推送 `711381e..a9daee4`） |
| **Phase** | **P6 — Product Completion & Release Qualification** |
| **Active Work Packages** | P6-0 ✅ DONE（见 `docs/P6-0-Baseline-Stabilization-Record.md`）；P6-1A ✅契约已冻结（`docs/P6-P1A-Asset-Import-Contract.md`）；**P6-1B（glTF/OBJ/纹理实现）= NEXT** |
| **Open Blockers** | 无 |
| **Release Readiness** | **NOT RELEASE QUALIFIED** — 需 P6 Release Acceptance Matrix（roadmap §十四）全表通过 |
| **CI State** | GitHub Actions（Ubuntu gcc/clang、Windows Release、Windows RelWithDebInfo+ASan；dotnet `Nova.sln` + format gate）。本地权威清单：`tools/test_inventory.ps1` |

## 基线坐标（a9daee4 处冻结）

| 口径 | 数值 |
|---|---|
| Native gtest（全量，Debug=Release） | 1008 发现 = 1008 执行；**1006 PASS / 1 FAIL / 1 SKIP** |
| CTest 注册项 | 19（18 目标 + content fixture） |
| P5 回归子集（5 目标） | 244/244 Debug+Release |
| Managed MSTest（静态计数） | 378 |
| 引擎构建 | EngineCore / EditorBridge / editor_avalonia：Debug+Release 0 error |

FAIL 项：`EditorHostAbiTest.D3PickRequestPumpFetchHappyPath`（PICK-01，P6-2 范围）。
SKIP 项：`ShaderCompileRule.SkippedWithoutVulkanOrShaderc`（本机 SDK 环境性条件跳过）。
NEVER 混用上述口径（参见 P6-0 记录 §2）。

## Delta 日志（新记录追加在此处）

- 2026-11-05：P6-0 完成（远端同步 + 权威基线 + 保留策略），P6-1A 契约冻结。
  全量 1008 套件双配置跑通；test_pick_transport 构建注册缺陷修复（BUILD-01）。
EOF
