# Nova UI v0.2 Performance Baseline

> 运行日期：2026-10（本机 Windows / .NET 9 / Debug smoke）
> 注意：这是 CI smoke 预算，不是目标硬件正式基准。Release 测量需单独记录。

## 运行方式

```powershell
dotnet test tests/Nova.Editor.PerformanceTests/Nova.Editor.PerformanceTests.csproj `
  --logger "console;verbosity=detailed"
```

## 预算与实际（Debug）

| 测试 | 预算 | 本机观测 |
| --- | --- | --- |
| `UiStateStore.Set` P95（2000 次） | < 50 ms | 0.001 ms |
| 1 万节点 `EditPropertyCommand` P95（500 次 execute+undo） | < 50 ms | 0.000 ms（低于 Stopwatch 分辨率） |
| 1 万次索引查找 | < 500 ms | 2.426 ms |
| 1 万次状态写入分配量 | < 10 MiB | 1.31 MiB |
| 布局序列化+反序列化 P95 | < 50 ms | 0.016 ms |

Debug 下命令 P95 低于计时分辨率，说明单次命令开销远低于预算；这不代表复杂控件/渲染开销。

## 可信度限制

- 未包含 Avalonia 布局/渲染、绑定通知和文本整形。
- 未固定 CPU 频率、GC 模式、后台负载。
- 已测状态写入分配量；未测 UI 线程帧时间、Avalonia 布局/绘制耗时。
- 未在目标硬件（最低配置）上运行。

## 下一步

1. 在 Release + 固定数据下测 P95/P99，并记录机器信息。
2. 引入 headless 渲染后，测布局/绘制耗时与虚拟化容器数量。
3. 将结果写入 CSV（`docs/benchmarks/nova-ui-baseline.csv`）并按版本比较。
4. 为大型列表增加容器数量与可视区外创建数量的断言。
