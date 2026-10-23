# Nova UI v0.2 — 提交前审计与隔离验证

> 结论：Nova 工作集可作为独立提交候选；工作区中同时存在大量与 Nova 无关的既有修改/子模块脏状态，**不得使用 `git add .`**。
> 自动化行为测试、隔离目录构建、全新 NuGet 缓存构建均通过。像素级 CI 截图回归仍未实现。

## 1. 变更边界

### Nova 工作集（建议提交）

```
Nova.sln
Directory.Build.props
.github/workflows/nova-ui-ci.yml
src/Nova.UI.Abstractions/
src/Nova.UI.Core/
src/Nova.UI.Theme/
src/Nova.UI.Avalonia/
src/Nova.UI.Controls/
src/Nova.Editor.Services/
src/Nova.Editor.Views/
src/Nova.Editor.App/
tests/Nova.UI.Core.Tests/
tests/Nova.UI.Controls.Tests/
tests/Nova.Editor.IntegrationTests/
tests/Nova.Editor.PerformanceTests/
samples/Nova.UI.Playground/
tools/ui/
docs/Nova-UI-v0.2-Editor-Foundation.md
docs/Nova-UI-v0.2-Precommit-Audit.md
docs/ui-spec/
docs/decisions/
docs/migration/
docs/benchmarks/nova-ui-baseline.md
README.md（仅新增 Nova 章节）
.gitignore（仅新增 Nova 构建产物忽略项；注意同文件还有既有的 `gp01_hrc_a7_scratch/` 变更）
```

### 审计发现并修复

- 根 `.gitignore` 的旧规则 `*.app` 会把整个 `src/Nova.Editor.App/` 目录当作可执行文件忽略，导致 App 项目无法进入提交。
- 已在 `.gitignore` 末尾追加 `!src/Nova.Editor.App/`、`!src/Nova.Editor.App/**`，随后重新忽略该项目的 `bin/`、`obj/`。
- 对 Nova 工作集全部文件执行 `git check-ignore` 扫描：**0 个业务文件被忽略**。

### 明确排除

- `editor_avalonia/`、`bridge/`、`engine/`、`sandbox/`、`tests/CMakeLists.txt` 等既有修改：Nova 工作集未引用它们，`git diff` 中无 Nova 引用。
- `third_party/d3d12ma`、`third_party/openal-soft`、`third_party/vma` 的 `-dirty` 子模块状态：与 Nova 无关，不应纳入提交。
- 工作区中既有的未跟踪 scratch/docs/崩溃文件：与 Nova 无关。
- `artifacts/`、`src/**/bin|obj`、`tests/**/bin|obj`、`samples/**/bin|obj`、`TestResults/`：已由 `.gitignore` 忽略。

## 2. 干净目录隔离验证

在仓库外的隔离目录（仅复制 Nova 工作集，不含 bin/obj、不含既有 dirty 修改）执行；另在 `git worktree add --detach <temp> HEAD` 的真实干净 worktree 上重复同一套命令，结果一致：

| 步骤 | 命令 | 退出码 |
| --- | --- | --- |
| Restore（缓存） | `dotnet restore Nova.sln` | 0 |
| Build | `dotnet build Nova.sln -c Release --no-restore` | 0 |
| Test | `dotnet test Nova.sln -c Release --no-build` | 0 |
| Format | `dotnet format Nova.sln --verify-no-changes --no-restore` | 0 |

真实 worktree 上同样得到 restore=0、build=0、test=0、format=0，确认不依赖工作区中未跟踪/既有修改。

### 全新 NuGet 缓存 + 显式 nuget.org

```bash
NUGET_PACKAGES=/tmp/nova-nuget-empty \
  dotnet restore Nova.sln --source https://api.nuget.org/v3/index.json
```

- Restore 退出码：0
- 随后 build / test / format 均退出码：0
- 测试总数：24 Core + 6 Controls + 23 Integration + 5 Performance = **58 passed / 0 failed**
- Build 结果：**0 Warning(s), 0 Error(s)**（全新 NuGet 缓存 + 显式 nuget.org；本机默认源下的 NU1900 仅因 `azure-cn` 不可达，不是代码警告）
- 说明：在本机默认 NuGet 配置中 `azure-cn` 不可达但 `nuget.org` 可用；之前的 restore 依赖全局缓存。使用显式 `nuget.org` 与全新缓存后仍可重建，因此不依赖未跟踪的本地包状态。
- CI 使用 `actions/setup-dotnet` 的默认 NuGet 配置，需保持网络可达；若未来改为完全离线构建，应单独提供离线包源并写入文档。

## 3. 命令与历史边界审查

已新增/加固：

| 语义 | 实现 | 测试 |
| --- | --- | --- |
| 非法输入不产生历史 | schema 校验先于命令创建 | `Invalid_property_is_rejected...` |
| 无变化提交不产生空命令 | `EditPropertyCommand.CanExecute()` 比较旧/新值 | `No_op_property_submission...` |
| Undo/Redo 后 Inspector 显示当前模型值 | `History.Undone/Redone` → ViewModel 刷新行 | `Inspector_row_commit_and_undo...` |
| 节点删除后陈旧命令不可 Undo | `CanUndo()` + `CommandHistory.Undo()` 检查 | `Stale_property_command_cannot_undo_after_node_removal` |
| 跨工作区命令拒绝 | `SceneDocument.ContextId` + `CommandHistory` context token | `Command_from_another_workspace_is_rejected` |
| 执行异常不进入历史 | 先 `Execute` 后 `Record` | `Command_that_throws_is_not_recorded` |
| 命令部分修改后抛异常 | 当前命令均为单字段/单结构修改；投影订阅者异常已被隔离 | **未来复合模型命令必须补事务/补偿** |
| 投影订阅者抛异常 | `UiStateStore`/`EditorLogService` 隔离订阅者异常；`EditPropertyCommand` 捕获投影回调异常 | `Projection_failure_does_not_split_model_and_history` |

未实现：切换场景/项目的完整 UI 生命周期。当前只提供文档上下文 token 与陈旧命令防护，不应宣称已完成工作区生命周期管理。

## 4. 布局持久化故障路径

已覆盖：

| 场景 | 测试 |
| --- | --- |
| 正常保存/恢复 | `Saved_layout_round_trips_through_disk` |
| 文件缺失回退 | `Missing_layout_uses_defaults_and_does_not_throw` |
| 损坏 JSON 回退 + 隔离 | `Corrupt_layout_falls_back_and_is_quarantined` |
| 未知 schema 不被静默迁移 | `Unknown_schema_is_quarantined_instead_of_silently_migrated` |
| 原子替换失败保留旧文件、清理 temp | `Failed_atomic_replace_keeps_the_previous_layout_and_cleans_temp` |
| 并发保存串行化、无 temp 残留 | `Concurrent_saves_are_serialized_and_leave_no_temp_files` |
| 面板显隐保留实际 Splitter 宽度 | `Panel_toggle_measures_and_preserves_splitter_width` |
| 未知主题 ID 回退 | `Unknown_theme_id_falls_back_without_breaking_layout` |

仍未覆盖：

- 隔离文件改名失败时的专门故障注入（当前 `QuarantineAsync` 已 catch，但缺少独立测试）。
- 保存与窗口关闭并发时的最终写入顺序断言（当前由 `SemaphoreSlim` 串行化，缺少专门 close race 测试）。
- 连续快速显隐/拖拽下的 UI 级过期测量（有模型级测试，无 headless UI 级测试）。

## 5. 截图基线定位

| 层级 | 状态 |
| --- | --- |
| 自动化行为测试 | 已接入 CI |
| 人工视觉验收 | 已用真实窗口截图验证 Dark/Light、Editor/Playground |
| CI 像素级回归 | **未实现**；离线环境缺 `Avalonia.Headless` |

不要把人工截图流程描述为 CI 像素回归。补齐前建议保持现状，不引入不可靠的无窗口渲染替代品。

## 6. 提交建议

提交标题建议：

```
feat(nova): add UI foundation and editor shell vertical slice
```

提交前建议使用显式路径暂存，例如：

```bash
git add Nova.sln Directory.Build.props .github/workflows/nova-ui-ci.yml
git add src/Nova.UI.Abstractions src/Nova.UI.Core src/Nova.UI.Theme
git add src/Nova.UI.Avalonia src/Nova.UI.Controls
git add src/Nova.Editor.Services src/Nova.Editor.Views src/Nova.Editor.App
git add tests/Nova.UI.Core.Tests tests/Nova.UI.Controls.Tests
git add tests/Nova.Editor.IntegrationTests tests/Nova.Editor.PerformanceTests
git add samples/Nova.UI.Playground tools/ui
git add docs/Nova-UI-v0.2-Editor-Foundation.md docs/Nova-UI-v0.2-Precommit-Audit.md
git add docs/ui-spec docs/decisions docs/migration docs/benchmarks/nova-ui-baseline.md
```

`.gitignore` 含既有 `gp01_hrc_a7_scratch/` 与本工作集的 Nova 忽略项，建议逐 hunk 暂存；`README.md` 只有新增的 Nova 章节，可整文件暂存：

```bash
git add README.md
git add -p .gitignore
```

提交说明中应明确：

- 已交付：UI tokens、P0 控件基础范围、Editor Shell 纵向切片、命令/撤销基础、布局持久化、Playground、测试与 CI。
- 未交付：完整 Docking、Timeline、复杂 DSL、真实视口后端、CI 像素截图回归、完整跨场景工作区生命周期。

## 7. 结论

- Nova 工作集在独立目录 + 全新 NuGet 缓存下可构建、可测试、格式检查通过。
- 旧 native/gate 代码未被 Nova 依赖；但工作区本身存在无关 dirty 修改，提交时必须显式选择路径。
- 命令历史已加入上下文隔离与陈旧命令防护；布局故障路径覆盖明显增强。
- 下一阶段建议先完成“工作区/场景生命周期”切片，再进入 Docking/Timeline/视口。
