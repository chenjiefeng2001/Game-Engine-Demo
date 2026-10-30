# Content Test Fixture Restoration — Dogfood Manifests

**状态**：`BACKLOG — OUT OF T-5 SCOPE — NOT AUTHORIZED`
**日期**：2026-10-31
**关联裁决**：T-5 分支裁决（`test_content` 基线例外）

---

## 1. 事实

全量 CTest Debug/Release 均为 17/18 target；唯一失败目标为 `test_content`，5 个 dogfood 用例失败：

- `ContentDogfood.DF03_Smoke_LoadRunTimerExpire`
- `ContentDogfood.DF03_EntityCount_MultiScript`
- `Dogfood05.LoadAndRun`
- `Dogfood06.LoadAndRun`
- `Dogfood07.LoadRunRestart`

失败原因：测试请求的以下 manifest 在 HEAD 中不存在：

```text
assets/scenes/dogfood03.manifest.json
assets/scenes/dogfood05.manifest.json
assets/scenes/dogfood06.manifest.json
assets/scenes/dogfood07.manifest.json
```

现有报告未发现这些失败与 T-5 DebugDraw 改动相关；记为 T-5 范围外基线问题。没有把用例改为 Skip，也没有修改测试来伪装全绿。

---

## 2. 建议范围

1. 确认四个缺失 manifest 是否属于应纳入版本控制的规范测试资产。
2. 如果是，恢复正确资产及其依赖（scene、script、texture 等），保持测试内容验证。
3. 如果它们应由 Content Pipeline 生成，检查测试准备阶段是否缺少生成步骤。
4. 保留五个 dogfood 用例的实际内容断言，不以无条件跳过作为修复。
5. 恢复后单独验证 `test_content`，再运行全量 CTest Debug/Release。

---

## 3. 验收

- `test_content` 恢复通过；
- 全量 CTest 达到真实全绿，或所有剩余失败都有独立、可复现的基线记录；
- 不以修改/删除断言、无条件 Skip 或修改测试期望来规避资产缺口；
- 记录实际构建配置、测试命令、结果与警告数量。

---

## 4. 边界

- 本任务不属于 T-5 v1，不影响 T-5 按范围封存。
- 未获授权前不修改 `assets/scenes`、测试代码或 Content Pipeline。
- 不为了全量 CTest 全绿而回滚当前 T-5 提交。
