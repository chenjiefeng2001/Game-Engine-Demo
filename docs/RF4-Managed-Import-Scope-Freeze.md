# RF-4 后续（任务 #141）：Gate A 范围冻结

**日期**：2026-10-29
**状态**：`GATE A — APPROVED (方案 A); IMPLEMENTED`
**授权边界**：T-2 §4 的后续任务；T-2 仍为 `CLOSED — SCOPE-BOUND`。
**实施结果**：见 `docs/RF4-Managed-Import-Closure.md`。

---

## 1. 任务身份

- 任务：#141 `RF-4 后续：托管 UI 端到端导入自动化`。
- 目标：在真实 `editor_avalonia` 生产路径中完成一次可重复的资产导入 E2E，
  覆盖 T-2 §4 明确列为未验证的链路：
  - 真实窗口启动；
  - 文件选择器操作；
  - 托管事件处理；
  - UI → 原生导入服务 → RevisionStore 的完整运行时链路；
  - 成功/失败反馈；
  - 工程重载后的资产可见性。
- 不是重新打开 T-2，也不是把 T-2 的 RF-4 历史例外改写成“当时已经通过”。

---

## 2. 现有实现与既有证据

### 2.1 生产导入链路（已存在）

```text
MainWindow.OnImportAsset                 editor_avalonia/MainWindow.axaml.cs:453-477
  └─ StorageProvider.OpenFilePickerAsync  （真实 Windows 文件选择器）
  └─ Vm.ImportAssetFromPath(src, type)    editor_avalonia/ViewModels/MainViewModel.cs:868-884
       └─ EditorHostService.ImportAsset   editor_avalonia/Services/EditorHostService.cs:1005
            └─ EngineHost op 11           editor_avalonia/Native/EditorBridgeApi.cs:112
                 └─ EditorSession::ImportAssetInternal
                      └─ 统一导入服务 + RevisionStore::Publish
```

UI 按钮：`editor_avalonia/MainWindow.axaml:221`（`ImportAssetBtn`，`Import...`）。

### 2.2 已有、但不足以关闭 RF-4 的证据

| 证据 | 覆盖 | 缺口 |
|---|---|---|
| `ImportServiceTest`（8/8，`tests/test_bridge/test_import_service.cpp`，记录见 `docs/Content-v2-M1-E-Import-Service.md` §5/§6） | 源拷贝、GUID、冲突拒绝、RevisionStore 提交、失败不提交、Reattach | 不经过托管 UI |
| `ProjectRootEngineHostTest.SourceImportViaHostOpUsesUnifiedService` | EngineHost op 11 进入统一服务 | 不经过 UI 按钮/文件选择器 |
| `Phase3Gate.RunAssetBrowser`（`editor_avalonia/Services/Phase3Gate.cs:101-151`） | 通过 `Vm.ImportAssetFromPath` 导入、资产计数、GUID、保存、重开、绑定 | **直接调用 ViewModel，不经过 `OnImportAsset`/真实文件选择器** |
| `T1ManagedAcceptanceGate`（`editor_avalonia/Services/T1ManagedAcceptanceGate.cs`） | 真实窗口内用 `window.FindControl<Button>` 点击真实按钮，验证 Inspector | 未覆盖导入按钮/文件选择器；导入路径未接入 |
| T-2 §4 RF-4 | 明确记录真实窗口/文件选择器/托管事件/UI→原生链路未验证 | **不得用上述任何一项代替** |

结论：**Phase3Gate 的导入证据是 VM/服务层证据，不是 RF-4 的 UI E2E 证据。**
#141 必须新增一个真正经过 `OnImportAsset` 与 Windows 文件选择器的门禁。

---

## 3. 最小 E2E 场景（冻结候选）

使用 `%TEMP%` 下的 scratch 工程与独立源目录；不写 repo fixture。

| # | 步骤 | 必须断言 |
|---|---|---|
| 1 | 启动真实 `editor_avalonia`，打开 scratch 工程（`assets/gp01` 副本） | 主窗口出现；工程打开成功；资产数 33 |
| 2 | 通过真实 UI 点击 `Import...`（`ImportAssetBtn`） | `OnImportAsset` 被触发；出现真实 Windows 文件选择器 |
| 3 | 在真实文件选择器中选择外部源文件（如 `m1import_pad.png`）并确认 | 选择器返回该绝对路径；UI 进入导入处理 |
| 4 | 等待导入完成 | Console 出现成功文本（`asset imported:`）；资产浏览器出现新资产；资产数 33→34 |
| 5 | 核对提交权威 | `.engine-content/current` 指向新 revision；revision 内 manifest 含 `assets/imported/tex/m1import_pad.png` 与新 GUID；旧 manifest 镜像一致 |
| 6 | 关闭并重新打开工程（进程重启优先；至少会话重开） | 新资产仍可见；path/GUID 与步骤 5 一致 |
| 7 | 失败路径：预置目标冲突（如 `assets/imported/tex/conflict.png` 已存在），再次通过真实 UI 导入同名源 | Console 出现 `IMPORT FAILED:`；资产数不变；`current` 字节不变（无假提交） |
| 8 | 证据 | UI 文本、进程退出码、scratch 路径、`current`/revision 字节、日志；失败时保留现场 |

### 3.1 成功判据

- 必须真实经过：真实窗口 → `ImportAssetBtn` → `OnImportAsset` → Windows 文件选择器 →
  `Vm.ImportAssetFromPath` → EngineHost op 11 → 统一导入服务/RevisionStore。
- 成功路径与失败路径都必须有 UI 可见反馈。
- 持久化断言以 `.engine-content/current` 及其引用的 revision 为准，不以“UI 提示成功”
  或内存返回值单独作为提交证据。
- 冷启动后资产身份（path + GUID）保持一致。

### 3.2 明确不在范围

- Rename、Collider、Physics、ScriptSave、Reattach UI、可配置导入目录、内容哈希去重；
- 真实导入器/M2、Asset Streaming/Cook（T-8）；
- 新增 Bridge ABI、修改 EngineHost op 11 语义、修改统一导入服务或 RevisionStore；
- 修改 T-2 冻结规则、T-1 RigidBody/物理契约、Collider schema；
- 为测试新增生产导入旁路（例如测试专用“直接导入路径”按钮）；
- commit / push / tag / merge。

---

## 4. 实现路径选项（需要裁决）

### 方案 A（推荐）：独立 UIA 集成测试 + 最小 host 启动入口

- 新增 `tests/EditorAvalonia.IntegrationTests`（`net9.0-windows`，
  `Microsoft.WindowsDesktop.App` + `System.Windows.Automation`，MSTest）。
- 编辑器增加一个**只负责打开 scratch 工程**的 harness 入口（如
  `--m1import-host <scratchRoot>`），不提供任何导入旁路；导入仍由真实按钮 + 真实 picker 驱动。
- 测试进程启动 `AvaloniaEditor.exe`，用 UIA 点击 `Import...`，在真实文件对话框中填入源路径并确认；
  读取 UIA 文本、文件/RevisionStore 状态；关闭进程后重启并验证资产。
- 优点：真实独立进程；真实 picker；测试代码不进入生产；UIA 文本是可审计的 UI 证据。
- 代价：新增测试项目与 UIA/桌面依赖；文件对话框 UIA 自动化需要 Gate B 先做可行性验证；
  无交互桌面时只能判 `BLOCKED`。

### 方案 B：编辑器内 Win32 门禁

- 在 `editor_avalonia` 内新增 `--m1import` gate；真实窗口内点击真实按钮，
  后台线程用 Win32 `FindWindow`/`SetForegroundWindow`/`SendInput` 操作文件对话框。
- 优点：不新增测试项目；复用现有 `--t1ui` 式 gate 运行方式。
- 代价：测试代码进入生产 app；原始 Win32 对话框自动化更脆弱；没有独立进程/UIA 文本证据；
  仍可满足“真实 picker”，但可审计性弱于 A。

### 方案 C：仅扩展现有 Phase3Gate

- 继续调用 `Vm.ImportAssetFromPath` 或把它包装成按钮点击。
- 结论：**不能关闭 RF-4**。它不经过真实文件选择器；只能作为服务层/VM 层证据。
  若选择 C，#141 必须保持 `UNVERIFIED` 或 `BLOCKED`，不能写成 PASS。

### 方案 D：维持 RF-4 例外，不启动 #141

- 保留 T-2 的 `UNVERIFIED — ACCEPTED SCOPE EXCEPTION`；#141 保持 pending。
- 只有在明确不投入 UI 自动化成本时选择；与“#141 作为下一项”的路线不一致。

---

## 5. 推荐方案与理由

推荐 **方案 A**：

1. 它把“真实文件选择器”作为不可绕过的门禁，直接对应 T-2 §4 的四项未验证链路；
2. 测试代码不进入生产程序，符合“不新增测试专用导入旁路”的边界；
3. UIA 文本 + 文件/RevisionStore 证据 + 进程重启，能形成可复核的闭环；
4. 现有 `Nova.Editor.IntegrationTests` 已有 `System.Windows.Automation` 的真实进程/UIA 模式可参考，
   风险主要是文件对话框控件识别，可在 Gate B 先用一个最小探针验证，失败则回退方案 B 并记录原因。

**需要确认的细节**：

- 是否接受一个只打开 scratch 工程、不绕过导入的 `--m1import-host` harness 入口；
  若不接受，则需同时自动化 Open Project 的文件选择器，范围与成本显著增加。
- 冷启动优先用**进程重启**（更强）；若实现受限，至少 `ResetSession` + 重开工程，并在记录中注明。
- 失败路径采用**目标冲突**（可确定、无需修改生产服务）：预置 `assets/imported/tex/conflict.png`，
  再通过真实 UI 导入同名源，断言 `IMPORT FAILED` 且 `current` 不变。
- 新测试项目是否加入 `Nova.sln`；可先独立 `dotnet test`，不强行改解决方案。

---

## 6. 阶段门计划（批准后）

| 门 | 工作 | 退出条件 |
|---|---|---|
| Gate B 最小实现 | 方案 A 的 host 入口 + UIA 测试 + runner；不改生产导入语义 | 实现仅触及新测试/新 harness/docs；编译通过 |
| Gate C 定向测试 | 运行成功路径、失败路径、冷启动；Debug 与 Release 分别记录 | 定向用例通过；证据保存 |
| Gate D 回归 | 现有 managed 生产门（至少 `--prodpath`；如触及 T-1 相关则加 `--t1ui`）与必要的原生导入测试 | 相关门禁无回归；未运行项标 `NOT RUN` |
| Gate E 封存 | 写 `docs/RF4-Managed-Import-Closure.md`；更新 #141 状态 | `PASS` / `FAIL` / `BLOCKED` / `UNVERIFIED` 四态之一 |

**#141 完成后**：T-2 仍为 `CLOSED — SCOPE-BOUND`；RF-4 的历史例外不改写；
#141 只关闭自己的验收缺口。F2-S1 仍为独立事项，另行排期裁决。

---

## 7. 当前状态与待批准项

- Gate 0：`PASS`（见 `docs/RF4-Managed-Import-Stage0-Baseline.md`）。
- Gate A：本文件为**提案**，尚未批准；未创建 #141 源码/测试。
- 待批准：
  1. 选择方案 A / B / C / D；
  2. 若选 A，是否接受 `--m1import-host` 仅用于打开 scratch 工程；
  3. 冷启动是否要求进程重启（推荐）还是会话重开即可；
  4. 失败路径采用目标冲突；
  5. 新测试项目是否加入 `Nova.sln`。

在获得上述裁决前，不进入实现。

---

## 8. 裁决与实施结果（2026-10-29）

- 裁决：**方案 A**（独立 UIA 集成测试 + 最小 host 入口）。
- 已接受的细节：
  - `--m1import-host` 只打开 scratch 工程，不提供导入旁路；
  - 冷启动使用**进程重启**；
  - 失败路径使用**目标冲突**；
  - 新测试项目独立 `dotnet test`，不加入 `Nova.sln`。
- 实施与证据：`docs/RF4-Managed-Import-Closure.md`。
- 本阶段未新增 Bridge ABI、未修改统一导入服务/RevisionStore、未修改 T-1/T-2 功能语义。
