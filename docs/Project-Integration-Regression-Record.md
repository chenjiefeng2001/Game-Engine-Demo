# Project Integration Regression Record

**状态**：`PASS — REGRESSION BASELINE ESTABLISHED`
**日期**：2026-11-01
**分支**：`avalonia`
**HEAD**：`b10e0cf84442602932d365c2445ce49bcaf8324a`
**审计类型**：P2 — Project-wide Regression & Integration（重新执行验证，建立当前 HEAD 基线）

---

## 1. Native 验证矩阵

| 模块 | Debug | Release | 备注 |
|---|---|---|---|
| `test_core` | 71/71 PASS | 71/71 PASS | Vector3 9 / Matrix4 3 / StackAllocator 8 / Time 23 / LogRing 20 / LogRingIntegration 8 |
| `test_bridge` | 112/112 PASS | 112/112 PASS | B0 103 + B3 5 + B2 4 新增 |
| `test_pick_transport` | 9/9 PASS | 9/9 PASS | |
| `test_hrc` | 15/15 PASS | 15/15 PASS | |
| `test_content` | 116/116 PASS | 116/116 PASS | dogfood manifests 已恢复（P4-A 候选可关闭） |
| Native 合计 | 323/323 | 323/323 | |

### 1.1 历史例外确认

| 项目 | 当前状态 | 说明 |
|---|---|---|
| T-5 RenderGraph SG6 | 未涉及（隔离状态保留） | 不因 P2 强行恢复 |
| T-5 `test_content` 基线 | **PASS** (116/116) | dogfood manifests 已恢复，P4-A 候选可关闭 |
| T-6 GL loss | KNOWN UNVERIFIED | 保留 |
| T-6 protocol violation | KNOWN UNVERIFIED | 保留 |
| T-6 10k UIA | KNOWN UNVERIFIED | 保留 |
| T-7 非 Owner Thread / ASan / TSan | 维持原有边界 | 未扩展 |
| UI-H2 多显示器 / 混合 DPI | 未涉及 | 不推定已支持 |

---

## 2. Managed / Editor 验证

| 测试 | Debug | Release |
|---|---|---|
| `T6BLoggingAbiTests` (4) | 4/4 PASS | 4/4 PASS |
| `T6ConsoleUITests` (1) | 1/1 PASS (10s) | 1/1 PASS (9s) |
| `AvaloniaEditor` 构建 | 0 error (6 既有 warning) | 0 error |

### 2.1 ABI 一致性确认

| 项目 | Native | Managed | 一致 |
|---|---|---|---|
| EngineHost ABI | v1.9 | v1.9 | ✅ |
| `EngineLogRecordV1` | 2392B | 2392B | ✅ |
| `EngineLogPollStatsV1` | 40B | 40B | ✅ |
| `EV_COUNT` | 14 | 14 | ✅ |
| `RuntimeResult` | 76B | 76B | ✅ |
| Pick packet | 32B | 32B | ✅ |

---

## 3. 测试结果分类

| 分类 | 说明 |
|---|---|
| PASS | 全部通过 |
| FAIL | 无 |
| BLOCKED | 无 |
| UNVERIFIED | T-6 GL loss、protocol violation native、10k UIA、partial failure host state |
| NOT RUN | RenderGraph SG6（隔离状态，未运行） |

---

## 4. P2 通过标准

```
PASS — REGRESSION BASELINE ESTABLISHED
```

所有已运行的测试全部通过。UNVERIFIED 项保持原样，不转换为 PASS。

---

## 5. P2 交付物

- `docs/Project-Integration-Regression-Record.md`（本文件）

---

## 6. 下一阶段建议

P2 通过。建议进入 P3（Delivery Readiness），重点：
1. 提交审查（白名单、无关改动、子模块、crashes/）
2. ABI 跨模块一致性最终确认
3. 交付裁决（READY / READY WITH ACCEPTED EXCEPTIONS / BLOCKED）
