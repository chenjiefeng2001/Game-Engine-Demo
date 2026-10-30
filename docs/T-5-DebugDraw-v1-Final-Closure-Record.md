# T-5 Generic DebugDraw v1 — Final Closure Record

**状态**：`T-5 v1 — PASS — SCOPE-BOUND / CLOSED`
**日期**：2026-10-31
**分支**：`avalonia`
**基线**：`74ad0999b35b1783ea9e58667966d3e84917dea8`
**最终裁决**：用户 2026-10-31 T-5 分支裁决

> 本记录按范围封存 T-5 v1；它不是“全仓库全量测试全部通过”的声明，也不授权生产 Editor 接线。

---

## 1. 交付与提交

| 阶段 | 状态 | 提交 / 记录 |
|---|---|---|
| Gate 0 | `PASS — READ-ONLY COMPLETE` | `docs/T-5-DebugDraw-Gate0-Scan.md` |
| Gate A | `PASS — ARCHITECTURE CONTRACT FROZEN — SCOPE-BOUND` | `docs/T-5-DebugDraw-GateA-Contract.md` |
| Gate B | `PASS — SCOPE-BOUND` | `019953e` |
| Gate C | `PASS — SCOPE-BOUND` | `c82012f` |
| D1 Box2D | `PASS — SCOPE-BOUND` | `454f136` |
| D3 Jolt | `PASS — SCOPE-BOUND` | `88e9477` |
| T-7 CMake 邻接提交 | 独立 T-7 测试注册 | `07117c1` |

分支相对 `origin/avalonia` ahead 7；**未 push**。本轮文档收尾未执行 Git 写入，新增/修改文档尚未提交。

---

## 2. 最终验收

| 项目 | 结果 |
|---|---|
| `test_debugdraw` | 22/22 Debug；22/22 Release |
| 组合 CTest（debugdraw/pick/renderer/physics） | 4/4 Debug；4/4 Release |
| `test_physics` | 71/71 Debug；71/71 Release |
| RenderTest / PhysicsTest 构建 | Debug/Release 均通过 |
| 全量 CTest | **17/18 — NOT FULL PASS** |
| `test_content` | **FAIL — KNOWN BASELINE ASSET GAP** |
| 构建警告 | 每个 `EngineCore` 构建 2 条既有 LNK4006；未宣称零警告 |

`test_content` 的 5 个 dogfood 用例因 `assets/scenes/dogfood03/05/06/07.manifest.json` 在 HEAD 中缺失而失败；未发现与 T-5 改动相关。该问题不进入 T-5 v1，另见 `docs/T-5-Content-Dogfood-Manifest-Restoration-Task.md`。没有通过 Skip 或修改测试来伪造全绿。

---

## 3. D5-11 修订

D5-11 已按 `docs/T-5-DebugDraw-D5-11-Release-Semantics-Amendment.md` 正式修订：

- 通用 DebugDraw 在 Debug/Release 均可用；
- Jolt 物理 DebugDraw 适配由 `JPH_DEBUG_RENDERER` 配置决定，不再规定 Debug-only；
- 当前项目 `DEBUG_RENDERER_IN_DEBUG_AND_RELEASE=ON`，因此 Jolt 适配在 Debug/Release 均可用；
- 适配器可用不等于 Jolt DebugRenderer 全部功能已实现；残余能力见下节。

原 D5-11 文本保留在 Gate A 契约中，§6.1 构建观察保留。

---

## 4. 残余与延期

| 项目 | 状态 | 是否阻塞 T-5 v1 |
|---|---|---|
| Jolt `DrawGeometry` 形状绘制 | D3 已修复（独立任务） | 否 |
| Jolt `DrawText3D` | no-op，延期 | 否 |
| Jolt LOD camera position | 未接入，延期 | 否 |
| Jolt solid 模式 | 仍线框化，延期 | 否 |
| 生产 EngineHost / Bridge / Editor 接线 | `NOT AUTHORIZED — DEFERRED` | 否 |
| RenderGraph / RHI 多后端 | v1 非目标 | 否 |
| 完整 ASan/TSan / 性能基准 | 未执行 | 否 |
| Content dogfood manifest 恢复 | 独立 Content 任务 | 否 |

Jolt 残余清单：`docs/T-5-DebugDraw-Jolt-DebugRenderer-Residual-Capabilities.md`。

---

## 5. 范围边界

T-5 v1 的完成条件是通用 DebugDraw 服务、GL 4.6 后端、2D/3D adapter、Sandbox/Legacy 接入和定向门禁，不是生产编辑器接入，也不是 Jolt DebugRenderer 的功能完整性验收。生产 Editor 接线仍需要单独的 3D Viewport 契约、ABI/UI 授权和真实进程验收。

---

## 6. 最终状态

```text
T-5 v1 — PASS — SCOPE-BOUND / CLOSED
```

不支持以下表述：

- 全仓库全量测试通过；
- `test_content` 通过；
- 生产编辑器已接入；
- Jolt DebugRenderer 全部能力已实现；
- 零警告。
