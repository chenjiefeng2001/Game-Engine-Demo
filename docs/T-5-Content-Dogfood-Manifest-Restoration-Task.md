# Content Test Fixture Restoration — Dogfood Manifests

**状态**：`RESOLVED — CTEST FIXTURE SETUP`
**日期**：2026-10-31
**关联裁决**：T-5 分支裁决（`test_content` 基线例外）；后续用户批准修复

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

---

## 5. 修复（2026-10-31）

根因不是测试断言错误：dogfood scene/manifest 由 `AssetBakery --dogfood-scenes` 生成，
且 `.gitignore` 排除了 `*.scene` / `*.manifest.json`。CI 已在测试前生成 fixture，
本地 CTest 缺少同等准备步骤。

实现：

- `tests/CMakeLists.txt` 为 `test_content` 增加 CTest fixture setup：
  - `content_fixture_setup` 调用 `$<TARGET_FILE:AssetBakery> --dogfood-scenes assets/scenes`
  - `FIXTURES_SETUP content_dogfood_fixtures`
  - `test_content` 设置 `FIXTURES_REQUIRED content_dogfood_fixtures`
  - `test_content` 依赖 `AssetBakery` target，保证本地单独构建测试时生成器存在
- 不提交生成的 `.scene` / `.manifest.json`；它们仍由 `.gitignore` 排除。
- 不修改 dogfood 测试断言，不增加 Skip。

验证（在删除已生成 fixture 后由 CTest 自动重建）：

| 目标 | Debug | Release |
|---|---:|---:|
| `test_content` | 107 passed / 1 skipped | PASS |
| 全量 CTest（含 fixture setup test） | 19/19 | 19/19 |

全量 CTest 的 19 项包含 `content_fixture_setup` 本身与 18 个既有 target；不是把 5 个 dogfood 用例改成 Skip。
