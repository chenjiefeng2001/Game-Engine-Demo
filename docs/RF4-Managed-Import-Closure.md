# RF-4 后续（任务 #141）：Managed UI Import E2E 关闭记录

**日期**：2026-10-29
**状态**：`#141 — PASS`
**范围**：真实 `editor_avalonia` 生产窗口中的资产导入 E2E；T-2 仍为
`CLOSED — SCOPE-BOUND`，RF-4 的历史范围例外不改写。
**未执行**：commit / push / tag / merge；未修改 T-2 导入服务/RevisionStore 语义；
未修改 T-1 RigidBody/物理契约；未新增 Bridge ABI。

本记录只列实际修改与实际运行结果。失败重跑日志保留，不因最终通过而删除。

---

## 1. 裁决与边界

- 任务：#141（`RF-4 后续：托管 UI 端到端导入自动化`）。
- Gate A 选择：**方案 A**（独立 UIA 集成测试 + 最小 host 入口）。
- 验收结论：**PASS**。
- T-2 的 RF-4 历史状态仍为 `UNVERIFIED — ACCEPTED SCOPE EXCEPTION`；
  #141 只关闭自己的验收缺口，不把 T-2 的当时证据改写成“已通过”。
- F2-S1 仍为独立事项；本阶段未启动 P1/T-4 等后续任务。

---

## 2. 实际修改

### 2.1 新增

| 文件 | 作用 |
|---|---|
| `editor_avalonia/Services/M1ImportHostBootstrap.cs` | `--m1import-host <scratch>`：只打开 scratch 工程并把标题设为 `Avalonia Editor — M1IMPORT-HOST`；**不提供导入旁路** |
| `tests/EditorAvalonia.IntegrationTests/EditorAvalonia.IntegrationTests.csproj` | `net9.0-windows` UIA 集成测试项目（`Microsoft.WindowsDesktop.App` + `System.Windows.Automation` + MSTest） |
| `tests/EditorAvalonia.IntegrationTests/ManagedImportE2ETests.cs` | 真实进程/真实按钮/真实文件选择器的成功、失败、冷启动 E2E |
| `tools/run_m1import_gate.ps1` | Debug/Release 构建 + 运行 UIA 门禁的 runner |
| `docs/RF4-Managed-Import-Stage0-Baseline.md` | Gate 0 只读基线 |
| `docs/RF4-Managed-Import-Scope-Freeze.md` | Gate A 范围冻结（已批准/已实施） |
| `docs/RF4-Managed-Import-Closure.md` | 本关闭记录 |

### 2.2 修改（两处生产 UI 文件均为本轮之前已有工作树改动）

| 文件 | 本切片新增内容 |
|---|---|
| `editor_avalonia/MainWindow.axaml.cs` | 注册 `--m1import-host`；成功时保持窗口运行，失败时退出非零 |
| `editor_avalonia/MainWindow.axaml` | 仅增加 UIA 元数据：`AssetSearchBox`、`ImportAssetBtn`、`AssetCountText`、`AssetList`、`ConsoleList` 的 AutomationId；无布局/行为变化 |

**未修改**：`bridge/**` 的导入/ABI、`engine/**` 的 Content/RevisionStore/导入服务、
T-1 RigidBody/Physics、Collider schema、任何 T-2 关闭记录。

---

## 3. 执行的 E2E 场景

测试：`ManagedImportE2ETests.Managed_import_via_real_button_and_picker_commits_and_survives_restart`。

### 3.1 成功路径

1. 启动真实 `AvaloniaEditor.exe --m1import-host <scratch>`；真实窗口打开 scratch 工程；
2. 通过 UIA 选中 `Asset Browser`，找到并触发真实 `ImportAssetBtn`（`Import...`）；
3. 真实 Windows 文件选择器出现；在 `File name:` 中输入 `%TEMP%` 下的外部源 PNG 并确认；
4. 导入完成后：
   - Console 出现 `asset imported:`；
   - Asset Browser 资产数 **33 → 34**；
   - 新资产路径 `assets/imported/tex/<name>.png` 与新 GUID 在 Asset Browser 可见；
   - `.engine-content/current` 已发布；
   - `current` 引用的 revision manifest 含该路径与 GUID；
5. 关闭进程，以同一 scratch 重新启动；
6. Asset Browser 仍可见该资产，路径/GUID 与首次一致。

### 3.2 失败路径

1. 预先在 scratch 中创建目标冲突文件 `assets/imported/tex/<conflict>.png`；
2. 通过真实 `Import...` 按钮和真实文件选择器选择同名的外部源；
3. 原生统一导入服务拒绝（engine log：`import asset: target already exists`）；
4. UI Console 出现 `IMPORT FAILED:`；
5. `current` 字节与失败前完全一致；revision manifest 资产数不变；无冲突资产进入 manifest；
6. Asset Browser 资产数仍为 34。

### 3.3 证据读取方式

- UI 结果：`System.Windows.Automation` 读取真实窗口的 UIA 树（按钮、Tab、Asset Browser
  文本、Console 尾部文本）；不是 ViewModel 返回值。
- 持久化权威：读取 scratch 的 `.engine-content/current`，再读取其引用的
  `.engine-content/revisions/<N>/manifest.json`；不从 UI 成功提示推断提交。
- 冷启动：真实 `AvaloniaEditor.exe` 进程退出并重新启动同一 scratch。

---

## 4. 实际结果

| 项目 | 配置 | 结果 | 日志 |
|---|---|---|---|
| UIA Managed Import E2E | Debug | **1/1 PASS**（19.6071 s） | `build/m1import_gate_Debug.log`；runner 汇总 `build/m1import_runner_Debug.log` |
| UIA Managed Import E2E | Release | **1/1 PASS**（17.9119 s） | `build/m1import_gate_Release.log`；runner 汇总 `build/m1import_runner_Release.log` |
| 原生导入服务（`ImportServiceTest.*` + EngineHost 导入专项） | Debug | **9/9 PASS** | `build/m1import_native_import_Debug.log` |
| 原生导入服务（同上） | Release | **9/9 PASS** | `build/m1import_native_import_Release.log` |
| Managed `--prodpath` | Debug | **exit=0，ALL GREEN（含 production teardown）** | `build/m1import_regression_prodpath_Debug.log` |
| Managed `--prodpath` | Release | **exit=0，ALL GREEN（含 production teardown）** | `build/m1import_regression_prodpath_Release.log` |
| Managed `--t1ui`（T-1 A 回归） | Debug | **exit=0，T1UI-A PASS** | `build/m1import_regression_t1ui_Debug.log` |
| Managed `--t1ui`（T-1 A 回归） | Release | **exit=0，T1UI-A PASS** | `build/m1import_regression_t1ui_Release.log` |

### 4.1 构建

| 项目 | 配置 | 结果 |
|---|---|---|
| `editor_avalonia/AvaloniaEditor.csproj` | Debug | 成功；**6 warnings**（2×NU1900、1×CS8602、3×CS0414） |
| `editor_avalonia/AvaloniaEditor.csproj` | Release | 成功；**6 warnings**（同左） |
| `tests/EditorAvalonia.IntegrationTests` | Debug | 成功；**2 warnings**（2×NU1900） |
| `tests/EditorAvalonia.IntegrationTests` | Release | 成功；**2 warnings**（2×NU1900） |

未执行 clean rebuild；**不宣称零警告/零失败**。完整 CTest 与完整
`test_bridge`/`test_content` 套件在本切片中 **NOT RUN**；只运行了上表列出的定向门禁。

### 4.2 失败重跑记录（保留）

首个可用的 UIA 门禁不是一次通过。`build/m1import_gate_Debug_run1.log` … `run15.log`
保留了 15 次迭代；其中 run1–run14 失败，run15 及官方 runner 的 Debug/Release 通过。
主要失败与修正：

1. **文件对话框不接受 UIA 的 Open 按钮**：UIA 树中只暴露 `Cancel`，不暴露 Open；
   改用 `File name:` 的键盘输入（Ctrl+A + Unicode 路径）与 Enter/布局回退。
2. **Enter 键发送方式错误**：最初用 `KEYEVENTF_UNICODE + scan=0x0D`，行为不稳定；
   改为 `VK_RETURN` 的 `KEYINPUT`。
3. **Console 尾部行不可见**：`asset imported:` / `IMPORT FAILED:` 被 ListBox 滚动区遮挡；
   增加 `ConsoleList` AutomationId、`ScrollItemPattern`/`ScrollPattern` 与 Ctrl+End。
4. **窗口焦点/激活**：文件对话框虽为前台，但键盘/鼠标需先点击编辑框使其获得焦点；
   增加对话框激活与编辑框点击。

这些失败归因于**测试 harness 的 UIA/对话框自动化**，不是原生导入服务错误；
原生服务在失败路径按预期拒绝冲突，成功路径按预期提交。失败日志未删除。

---

## 5. 已证明 / 未覆盖

### 5.1 已证明（本切片）

- 真实 `editor_avalonia` 进程启动、真实窗口、真实 `ImportAssetBtn` 触发；
- 真实 Windows 文件选择器操作（文件路径选择）；
- `OnImportAsset` → `Vm.ImportAssetFromPath` → EngineHost op 11 → 统一导入服务；
- 成功导入：资产路径/GUID、Asset Browser 可见、revision/current 提交；
- 失败导入（目标冲突）：UI `IMPORT FAILED:`、无假提交、`current` 不变；
- 进程重启后资产路径/GUID 仍可见；
- 现有 `--prodpath` 与 `--t1ui` managed 门禁未回归。

### 5.2 明确未覆盖

1. **Open Project 文件选择器**：scratch 工程由 `--m1import-host` 打开；未自动化
   “File → Open Project”的文件选择器。
2. **文件对话框 Open 按钮**：UIA 不暴露该按钮；测试使用真实对话框的键盘输入 + Enter/
   布局回退，而不是 UIA 点击 Open。仍是真实 picker，但自动化方式有限。
3. **只测 Texture 导入**：未覆盖 Script 导入、Reattach UI（仍不存在）、可配置导入目录、
   内容哈希去重、覆盖确认。
4. **无头/CI**：需要交互式桌面；UIA 不可用时测试以 `Inconclusive` 结束，不冒充 PASS。
5. **单桌面/单 DPI/单事件**：未覆盖多显示器、DPI 变化、并发导入、极端路径。
6. **未跑完整回归**：完整 CTest、完整 `test_bridge`、`test_content` 未在本切片运行；
   只报告已运行的定向门禁。

---

## 6. 与 T-2、F2-S1 的关系

- T-2 仍为 `CLOSED — SCOPE-BOUND`；RF-4 历史状态仍为
  `UNVERIFIED — ACCEPTED SCOPE EXCEPTION`。本记录不追溯修改 T-2 的任何数字或证据。
- #141 关闭的是“后续 UI 导入 E2E”这一独立任务；它不表示 T-2 当时已经通过 RF-4。
- F2-S1（Collider Serialization）仍独立，未因本切片关闭，也未成为 P1 的前置条件。

---

## 7. 工作区与 Git

- 未执行 commit / push / tag / merge / reset / clean / stash。
- 新增/修改文件见 §2；`editor_avalonia/MainWindow.axaml` 与 `.axaml.cs` 在本切片之前
  已处于工作树修改状态，本切片只加入上述 host 入口与 UIA AutomationId。
- 未触碰 `crashes/`、三个脏子模块、`stash@{0}`、`p7_ui` worktree。
- 构建/测试日志写入 `build/m1import_*.log`；测试失败现场在失败时保留
  `%TEMP%\engine_m1import_*`，通过后清理。

---

## 8. 结论

**#141 — PASS**：真实 managed 编辑器的 Import 按钮与真实文件选择器已完成成功、失败和
进程重启三类验证；成功路径的 `current`/revision 与新资产 GUID 一致，失败路径无假提交。
T-2 的 RF-4 历史例外保持不变；F2-S1 仍需独立排期裁决。
