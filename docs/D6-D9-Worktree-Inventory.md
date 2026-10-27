# D6–D9 累积工作树范围盘点（2026-10-27，D9 关闭后的交接盘点）

- 目的：D9 = CLOSED — scope-bound 之后，进入 D10 之前，机械核对"实际变更 ↔ 测试
  证据 ↔ ADR 记录"三者是否相互对应。本盘点**不修改任何生产代码**，**不复跑测试**，
  **不清理或覆盖任何无关 dirty 文件**，工作树保持未提交。
- 数据口径：全部数字由 `git status --porcelain` / `git diff --name-only` /
  `git ls-files --others --exclude-standard` / `wc -l` / `grep -c '\[TestMethod\]'` /
  `find -newermt` 当场取得，命令写在每一节，可复现。回归数字引用 ADR-010 §11.2
  的封印结果，本盘点不重新执行。
- HEAD：`6017998 feat(nova): add UI foundation and editor shell vertical slice`；
  暂存区为空（`git diff --cached --name-only | wc -l` = 0）。

## 一、范围结论（先看这一节）

1. **实际累积范围大于"D6–D9"**。HEAD 之后从未提交，因此工作树里同时含
   Lifecycle v0.1（ADR-004）、D2–D5（ADR-005/ADR-006）、D6（ADR-007）、
   D7（ADR-008）、D8（ADR-009）、D9（ADR-010）六个阶段的全部产物。
   盘点必须按"ADR-004…ADR-010 = D2…D9 + Lifecycle"来核对，否则会把 D2–D5 的
   文件误认为超范围。
2. **Nova 范围内的变更 = 98 个文件、35 196 行**：
   - `git status` 折叠口径：23 个 ` M `（已跟踪、被修改）+ 38 个 `?? `（新增条目，
     其中含目录折叠项）；
   - 展开后实际文件：23 个已跟踪修改 + 75 个新文件 = 98；
   - 分组：`src/` 62 个文件 14 079 行、`tests/` 28 个文件 17 350 行、
     `docs/`（ADR-004…ADR-010 + `Nova-UI-Lifecycle-v0.1.md`）8 个文件 3 767 行。
3. **范围闭合有正向证据**：对工作树里全部 213 个非 Nova 条目（`engine`、`bridge`、
   `editor_avalonia`、`sandbox`、`assets`、`tools`、`third_party`、
   `tests/test_bridge`、`tests/test_scripting`、`CMakeLists.txt`、`README.md`、
   `.gitignore`、`docs/` 非 decisions 项等）逐一按 mtime 检查
   `-newermt "2026-10-25 00:00"`（D 系列工作时间窗），**命中 0 个**。
   即这些既有 dirty 文件保持原状，D2…D9 从未写入它们。
4. **测试证据与源码计数一致**：四个测试工程里 `[TestMethod]` 的源码计数
   `117 / 76 / 141 / 5 = 339`，与 ADR-010 §11.2 从零重建后 Debug 与 Release
   两次全量执行结果 `339/339` **逐项相等**；Integration 各文件的用例数合计
   也正好 141（13+2+5+13+7+6+21+13+3+8+11+7+3+8+7+14）。因此"通过的 339 项"
   与"工作树里存在的 339 项"是同一集合，没有缺席或多出。
5. **本盘点写完后新增的条目（D10-A 时窗）**：`docs/decisions/ADR-011-…md`（169 行）
   与本盘点文件自身（201 行）不在上面的 98 文件／35 196 行之内；按同一口径重跑，
   Nova 条目现为 **23 个 ` M ` + 40 个 `?? ` = 63**（原 61），非 Nova 条目仍为
   **213 个**且 mtime 扫描（`-newermt "2026-10-25 00:00"`）**命中 0 个**——即第 3 条
   的正向范围闭合证据在加入这两份文档后依然成立。98／35 196 是 D9-F 封印时点的数字，
   不追溯修改，以免破坏与 ADR-010 §11.1 的一致性。

## 二、按 ADR 归属的文件清单

标注口径：**"点名" = 该 ADR 正文里出现该文件名**；**"按阶段记录归属" = ADR 只按
门禁 ID 或类名记录，未写文件名**，由本节给出对应关系。两种归属都写明，避免让读者
以为 ADR 逐文件背书。

### 2.1 ADR-004（Lifecycle v0.1）与 UI foundation 附带修改

| 文件 | 行数 | 归属依据 |
| --- | --- | --- |
| `tests/Nova.Editor.IntegrationTests/LifecycleTests.cs` | 290（11 用例） | 点名（ADR-004/ADR-008） |
| `src/Nova.Editor.Services/Projects/EditorProject.cs`、`SampleProjectFactory.cs` | 47 / 22 | 按阶段记录归属（项目/工作区生命周期，未被任何 D 系列 ADR 点名） |
| `src/Nova.Editor.Services/Selection/SelectionService.cs`、`Model/SampleSceneFactory.cs` | 53 / 59 | 按阶段记录归属（既有服务的附带适配） |
| `src/Nova.UI.Avalonia/NovaThemeAdapter.cs`、`NovaUiBinding.cs`、`src/Nova.UI.Theme/ThemeManager.cs` | 634 / 97 / 70 | 按阶段记录归属（主题/绑定适配层，未点名） |
| `tests/Nova.UI.Controls.Tests/StateBindingTests.cs`、`tests/Nova.Editor.IntegrationTests/ShellViewModelTests.cs` | 32 / 163 | 按阶段记录归属 |

> 诚实提示：这一组是 D 系列之外的**既有未提交变更**，它们随 HEAD 之后的工作树一起
> 累积，但没有逐文件 ADR 背书。若将来要提交，必须先决定这组文件是否应与 D 系列同批
> 提交（本盘点不建议在此阶段动它们）。

### 2.2 ADR-005 / ADR-006（D2–D5：树模型、投影、交互、拖放）

| 文件 | 行数 | 归属依据 |
| --- | --- | --- |
| `src/Nova.UI.Core/Docking/DockIds.cs`、`DockNode.cs`、`DockTree.cs`、`DockTreeDigest.cs`、`DockValidation.cs`、`DockValidator.cs`、`DockNormalizer.cs`、`DockResult.cs`、`DockPanelIds.cs` | 26/101/788/66/24/170/181/43/13 | ADR-005 点名（`DockValidator` 另被 ADR-009 点名） |
| `src/Nova.UI.Avalonia/Docking/DockProjectionContracts.cs`、`DockProjectorRootHost.cs`、`DockSplitterThumb.cs` | 168/189/240 | ADR-006 点名（`DockProjectorRootHost` 未点名，按 D2/D6-D 缺陷修复记录归属） |
| `src/Nova.UI.Avalonia/Docking/DockDragContracts.cs`、`DockDragController.cs`、`DockDropTargeting.cs` | 164/959/39 | 按阶段记录归属（D5-A 拖放契约、D6-D 跨窗口会话） |
| `src/Nova.Editor.Views/Docking/EditorDockPanelFactory.cs`、`EditorDockWorkspace.cs` | 38/469 | ADR-006/008/010 点名（跨 D3…D9 持续演进） |
| `src/Nova.Editor.Views/Docking/{Console,Hierarchy,Inspector,Scene}PanelView.axaml(.cs)` | 25/31/46/24 + 33/45/58/45 | `.axaml` 由 ADR-006 点名；`.axaml.cs` 未点名，按同一组视图归属 |
| `tests/Nova.UI.Core.Tests/DockTreeTests.cs` | 833（25 用例，WM-01…WM-14/18） | ADR-005 点名 |
| `tests/Nova.UI.Controls.Tests/DockProjectionTests.cs` | 544（12 用例） | ADR-006 点名 |
| `tests/Nova.Editor.IntegrationTests/DockInteractionTests.cs`、`DockDragTests.cs`、`DockShellIntegrationTests.cs` | 669/589/582（13/13/13 用例） | ADR-006 点名（`DockDragTests` 另被 ADR-008 点名） |

### 2.3 ADR-007（D6：多根模型、宿主抽象、真实浮动窗口、跨窗口拖放）

| 文件 | 行数 | 归属依据 |
| --- | --- | --- |
| `src/Nova.UI.Core/Docking/DockWorkspace.cs`、`DockFloatRoot.cs`、`DockBounds.cs`、`DockWorkspaceResult.cs`、`DockWorkspaceDigest.cs`、`DockWorkspaceValidation.cs` | 866/60/17/45/56/105 | ADR-006/008/010 点名（`DockBounds`、`DockWorkspace*` 多数未点名，按 D6-B/D7-B 记录归属） |
| `src/Nova.UI.Avalonia/Docking/DockWorkspaceCoordinator.cs` | 2 281 | ADR-008/010 点名（D6-C/D6-D 主体，D9-C 在同一文件加唯一记录点） |
| `src/Nova.UI.Avalonia/Docking/DockPanelViewRegistry.cs`、`DockRootHostContracts.cs`、`DockWindowRootHost.cs`、`DockFloatWindow.cs`、`DockWorkspaceProjector.cs` | 235/156/373/217/788 | `DockFloatWindow`、`DockWorkspaceProjector` 点名；Registry 与 RootHost 未点名，按 D6-C/D6-D 记录归属 |
| `tests/Nova.UI.Core.Tests/DockWorkspaceTests.cs` | 1 006（25 用例） | 按阶段记录归属 |
| `tests/Nova.UI.Controls.Tests/DockWorkspaceCoordinatorTests.cs` | 2 061（39 用例） | ADR-010 点名（`LW-01`…`LW-11` 在 ADR-010 §10.1 作为主宿主侧证据被引用） |
| `tests/Nova.UI.Controls.Tests/DockCrossWindowDragTests.cs`、`DockFloatWindowLifecycleTests.cs` | 661/486（9/7 用例） | 按阶段记录归属（D6-D） |

### 2.4 ADR-008（D7：多根工作区持久化、Schema v2、启动恢复、退出快照）

| 文件 | 行数 | 归属依据 |
| --- | --- | --- |
| `src/Nova.UI.Core/Layout/DockWorkspaceSnapshot.cs`、`WorkspaceLayout.cs`、`WorkspaceLayoutSerializer.cs`、`LayoutValidator.cs` | 394/32/180/60 | ADR-008 点名（`LayoutValidator` 亦点名） |
| `src/Nova.Editor.Services/Layout/LayoutPersistenceService.cs`、`LayoutSaveScheduler.cs`、`DockPersistenceContracts.cs` | 189/129/74 | ADR-008 点名（`DockPersistenceContracts` 另被 ADR-010 点名） |
| `src/Nova.Editor.Services/EditorWorkspace.cs` | 1 162 | ADR-008/010 点名（D7-C 保存门控 + D9-D 命令 + §10.5 配对保存注释） |
| `src/Nova.Editor.App/App.axaml.cs`、`MainWindow.cs` | 44/99 | ADR-008 点名（`--layout-file` 生产入口、退出快照时序） |
| `src/Nova.Editor.Views/EditorShellView.axaml(.cs)`、`ViewModels/EditorShellViewModel.cs` | 105/346/462 | ADR-008/009/010 点名 |
| `tests/Nova.UI.Core.Tests/DockWorkspaceSnapshotTests.cs`、`LayoutSerializerTests.cs` | 788/81（16/4 用例） | 按阶段记录归属 |
| `tests/Nova.UI.Controls.Tests/DockWorkspaceRestoreTests.cs` | 247（3 用例） | ADR-008 点名 |
| `tests/Nova.Editor.IntegrationTests/LayoutPersistenceTests.cs` | 191（8 用例） | ADR-008 点名 |
| `tests/Nova.Editor.IntegrationTests/RealProcessRestoreTests.cs` | 978（8 用例） | ADR-008 点名（含 §11.3 复验的浮窗 Bounds 族） |
| `tests/Nova.Editor.IntegrationTests/Nova.Editor.IntegrationTests.csproj` | 28 | ADR-008/009 点名（`net9.0-windows` + `Microsoft.WindowsDesktop.App`，真实进程门禁的前提） |

### 2.5 ADR-009（D8：隐藏／未解析面板生命周期、Schema v3、Shell 动作）

| 文件 | 行数 | 归属依据 |
| --- | --- | --- |
| `tests/Nova.UI.Core.Tests/DockHiddenLifecycleTests.cs` | 670（10 用例，WM-20…WM-29） | 按门禁 ID 归属（ADR-009 §D8-B 记录 WM-20…WM-29） |
| `tests/Nova.Editor.IntegrationTests/DockHiddenPanelShellTests.cs` | 488（5 用例） | 按门禁归属（D8-D Shell 门禁，ADR-009 记录该阶段） |
| `tests/Nova.Editor.IntegrationTests/RealProcessHiddenPanelTests.cs` | 1 315（7 用例） | 按门禁 ID 归属（ADR-009 点名 RP-1…RP-7，文件本身未点名） |
| `tests/Nova.Editor.IntegrationTests/DockHiddenLifecycleEdgeTests.cs` | 321（2 用例） | 按门禁归属（D8-E headless 组合故障/Shell 重开） |
| `tests/Nova.Editor.IntegrationTests/EditorSaveStatusTests.cs` | 230（3 用例） | 按内容归属（断言 `StatusText`/`PersistenceSuppressed`，即 D8-D 后的状态栏可见文本修复） |
| `src/Nova.UI.Core/Docking/DockNode.cs`、`DockValidator.cs`、`src/Nova.UI.Abstractions/Commands.cs` | 101/170/118 | `DockNode`、`DockValidator` 由 ADR-009 点名；`Commands.cs` 由 ADR-010 点名 |

### 2.6 ADR-010（D9：布局 Undo/Redo）

| 文件 | 行数 | 归属依据 |
| --- | --- | --- |
| `src/Nova.UI.Core/Docking/DockLayoutSnapshot.cs`、`DockLayoutHistory.cs`、`DockWorkspaceResult.cs` | 66/221/45 | ADR-010 点名（含 `DefaultMaxUndoDepth = 100` 命名冻结） |
| `src/Nova.UI.Avalonia/Docking/DockWorkspaceCoordinator.cs`（D9-C 段）、`src/Nova.Editor.Views/Docking/EditorDockWorkspace.cs`（D9 段） | — | ADR-010 点名（唯一记录点 :2079、`MayReplayLayout` :822、`ResetToDefault` :208） |
| `src/Nova.UI.Core/Commands/CommandHistory.cs` | 156 | ADR-010 点名（仅作并存事实基线，**未改语义**） |
| `tests/Nova.UI.Core.Tests/DockLayoutHistoryTests.cs` | 939（17 用例，`Lh01`…`Lh17`） | ADR-010 §八 点名 |
| `tests/Nova.Editor.IntegrationTests/DockLayoutFaultMatrixTests.cs` | 877（7 用例，LM-01…LM-07） | ADR-010 §10.1 点名 |
| `tests/Nova.Editor.IntegrationTests/DockLayoutHistoryShellTests.cs` | 475（6 用例，LS-01…LS-06） | ADR-010 §10.5/§11.5 点名 |
| `tests/Nova.Editor.IntegrationTests/DockPersistencePolicyTests.cs` | 1 284（21 用例，含 LM-08、LW-12） | ADR-008/010 点名 |
| `tests/Nova.Editor.IntegrationTests/RealProcessLayoutUndoTests.cs` | 522（3 用例，含 RP-L1、RP-L2） | ADR-010 点名 |

## 三、门禁 ID ↔ ADR 记录 ↔ 所在文件（可反查）

| ID 族 | ADR 记录处 | 所在测试文件 | 用例数 |
| --- | --- | --- | --- |
| `Lh01`…`Lh17` | ADR-010 §八 | `DockLayoutHistoryTests.cs` | 17 |
| `WM-01`…`WM-19`（部分） | ADR-005/007 | `DockTreeTests.cs`（15）、`DockWorkspaceTests.cs`（4） | 19 命中 |
| `WM-20`…`WM-29` | ADR-009 §D8-B（`WM-20` 同时被 ADR-007 引用） | `DockHiddenLifecycleTests.cs` | 10 |
| `LW-01`…`LW-11` | ADR-010 §10.1 引用为主宿主侧证据 | `DockWorkspaceCoordinatorTests.cs` | 11 命中 |
| `LW-12` | ADR-010 §10.5/§11.5 | `DockPersistencePolicyTests.cs` | 1 |
| `LM-01`…`LM-07` | ADR-010 §10.1 | `DockLayoutFaultMatrixTests.cs` | 7 |
| `LM-08` | ADR-010 §10.5 | `DockPersistencePolicyTests.cs`（`DockLayoutHistoryShellTests.cs` 的注释亦交叉引用） | 1 |
| `LS-01`…`LS-06` | ADR-010 §10.5/§11.5 | `DockLayoutHistoryShellTests.cs` | 6 |
| `RP-1`…`RP-7` | ADR-009 §D8-E | `RealProcessHiddenPanelTests.cs` | 7 |
| `RP-L1`/`RP-L2` | ADR-010 §10.1 | `RealProcessLayoutUndoTests.cs` | 2（+1 个 D9-D Reset 门禁） |

未被 ID 覆盖的门禁（按文件用例数计入 339，但在 ADR 中以阶段叙述而非 ID 记录）：
`DockProjectionTests` 12、`DockInteractionTests` 13、`DockDragTests` 13、
`DockShellIntegrationTests` 13、`DockCrossWindowDragTests` 9、
`DockFloatWindowLifecycleTests` 7、`DockWorkspaceTests` 21（WM 之外的部分）、
`DockWorkspaceCoordinatorTests` 28（LW 之外的部分）、`DockWorkspaceSnapshotTests` 16、
`DockWorkspaceRestoreTests` 3、`LayoutSerializerTests` 4、`LayoutPersistenceTests` 8、
`RealProcessRestoreTests` 8、`LifecycleTests` 11、`ShellViewModelTests` 7、
`EditorSaveStatusTests` 3、`DockHiddenPanelShellTests` 5、
`DockHiddenLifecycleEdgeTests` 2、`StateBindingTests` 1、
`WorkspaceCommandChainTests` 14、`CommandHistoryTests` 等 Core 残余、
`InteractionBudgetTests` 5。

## 四、与 ADR 记录的偏差（必须随交接带走）

1. **ADR 普遍不逐文件背书**：90 个源码/测试文件（不含 8 个文档）中，**57 个被至少
   一份 ADR 按文件名点名，33 个只能靠"门禁 ID + 阶段叙述"归属**。未点名的 33 个：
   `CommandRouter.cs`、`NovaThemeAdapter.cs`、`NovaUiBinding.cs`、`ThemeManager.cs`、
   `SelectionService.cs`、`SampleSceneFactory.cs`、`EditorProject.cs`、
   `SampleProjectFactory.cs`、`DockBounds.cs`、`DockPanelViewRegistry.cs`、
   `DockProjectorRootHost.cs`、`DockRootHostContracts.cs`、`DockSplitterThumb.cs`、
   `DockWindowRootHost.cs`、`DockDragContracts.cs`、`DockDragController.cs`、
   `DockDropTargeting.cs`、`DockWorkspaceTests.cs`、`DockWorkspaceSnapshotTests.cs`、
   `DockHiddenLifecycleTests.cs`、`DockHiddenPanelShellTests.cs`、
   `DockHiddenLifecycleEdgeTests.cs`、`RealProcessHiddenPanelTests.cs`、
   `DockCrossWindowDragTests.cs`、`DockFloatWindowLifecycleTests.cs`、
   `LayoutSerializerTests.cs`、`EditorSaveStatusTests.cs`、`ShellViewModelTests.cs`、
   `StateBindingTests.cs`，以及四个面板视图的 `.axaml.cs`
   （其 `.axaml` 同伴被 ADR-006 点名）。本表把两种依据分开写清，后续不得声称
   "ADR 覆盖了每个文件"。
2. **`LW-*` ID 族的记录不完整**：`DockWorkspaceCoordinatorTests.cs` 里 39 个用例只
   有 `LW-01`…`LW-11` 有 ID，其余 28 个用例没有可反查的编号；ADR-007 记录 D6-C 时
   用的是叙述而非 ID。因此"D6-C 故障矩阵"的用例级可追溯性弱于 D7/D8/D9。
3. **非 Nova 的既有未提交变更与 D 系列同处一个工作树**：213 个非 Nova 条目
   （engine/bridge/editor_avalonia/tools/third_party/…）mtime 证明未被 D 系列触碰，
   但它们与 Nova 变更共享"未提交"状态。任何未来提交都必须先做范围切分，不能默认
   一起提交。
4. **累积跨度大于阶段名**：本盘点标题按用户措辞写为"D6–D9"，实测范围实为
   **Lifecycle v0.1 + D2…D9**（ADR-004…ADR-010），因为 HEAD 之后从未提交。
5. **残余风险仍按 ADR-010 §11.8 生效**：未指名的 2 项 Release 失败保持"未判定"；
   浮窗最大化／还原过渡期那次已指名失败保持"复验事实"；两者都不得在后续报告中改写。

## 五、盘点方法（可复现命令）

```bash
git rev-parse --short HEAD
git status --porcelain | grep '^ M ' | grep -cE 'Nova|docs/decisions'      # 23
git status --porcelain | grep '^?? '  | grep -cE 'Nova|docs/decisions'      # 38
git diff --name-only | grep -Ei 'nova|docs/decisions' > nova_modified.txt   # 23
git ls-files --others --exclude-standard src tests samples docs \
  | grep -Ei 'nova|docs/decisions' | grep -vE '/(obj|bin)/' > nova_new.txt  # 75
for p in $(cat nova_modified.txt nova_new.txt); do wc -l < "$p"; done        # 35 196 行合计
for d in Nova.UI.Core.Tests Nova.UI.Controls.Tests Nova.Editor.IntegrationTests Nova.Editor.PerformanceTests; do \
  grep -rho '\[TestMethod\]' tests/$d --include=*.cs | wc -l; done           # 117/76/141/5
# 非 Nova 路径未被触碰（D 系列时间窗）
git status --porcelain | sed -E 's/^( M |\?\? ) //' | grep -vEi 'nova|docs/decisions' \
  | while read -r p; do find "$p" -type f -newermt "2026-10-25 00:00" 2>/dev/null \
      | grep -vE '/(build|obj|bin|x64|Debug|Release)/'; done                 # 0 命中
```

## 六、下一步

- 本盘点即 D10 的入口条件之一（用户 2026-10-27 裁决）。
- D10-A 的范围与风险裁决见
  [ADR-011（草案）](./decisions/ADR-011-docking-multi-monitor-dpi-robustness.md)；
  该阶段第一交付物是验收契约与环境矩阵，不在契约批准前修改 Docking 适配层。
- 本盘点未执行任何构建或测试命令，也未修改任何生产代码。
