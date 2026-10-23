# Screenshot & Baselines

## 当前状态（v0.2）

自动像素截图回归 **尚未接入 CI**。原因：本仓库的离线环境没有 `Avalonia.Headless` 包，无法在不启动窗口/平台服务的情况下初始化 Avalonia 渲染。把没有执行过的“截图门禁”写成已完成会掩盖风险，因此这里明确记录为缺口。

已具备的替代基线：

1. **布局快照回归**：`WorkspaceLayoutSerializer` + `LayoutSerializerTests` 固定 schema、默认值、归一化、损坏回退。
2. **状态/交互基线**：Core、Controls 合约、Integration 测试覆盖稳定 ID、命令链、undo/redo、主题/布局切换。
3. **性能 smoke**：见 `docs/benchmarks/nova-ui-baseline.md`。
4. **手动窗口截图**：`tools/ui/capture_nova_screenshot.ps1`（Windows 交互桌面，使用 `PrintWindow` 避免被其他窗口遮挡，输出到 `artifacts/screenshots/`）。已用于发现并修复 `StyleKeyOverride` 缺失导致输入控件不可见的问题。

## 补齐计划（v0.2.x / v0.3）

- 引入 `Avalonia.Headless`（MSTest/XUnit 适配）与固定 DPI/字体环境；
- 启动 Playground 的 headless 版本，固定窗口尺寸 1280×800、Dark/Light 两套；
- 对 `Nova.UI.Playground` 的 P0 控件区域做像素差异阈值（例如 0.1%）；
- 基线存 `tests/baselines/`，CI 上传 diff artifact；
- 截图前禁用动画、固定随机数据/时间戳；
- 对 Editor Shell 只比较布局区域，避免 Console 时间戳导致不稳定。

## 手动截图

```powershell
pwsh tools/ui/capture_nova_screenshot.ps1 -Project samples/Nova.UI.Playground
```

脚本会启动应用、等待窗口句柄、截取窗口区域并保存 PNG。它依赖交互式桌面，**不是** CI 门禁。
