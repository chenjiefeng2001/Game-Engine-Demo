# Dogfood-04 — Developer Experience Baseline Report

> **日期**: 2026-08-23
> **方法**: 模拟首次使用者从空项目到可玩游戏的完整工作流，逐步记录每个摩擦点
> **测试**: `DogfoodDXTest.cpp`（test_content 套件内，25/25 PASS）
> **规则**: 零 C++ Gameplay 修改；仅使用冻结 API + 已有 Editor UI

---

## 审计工作流（10 步）

| 步骤 | 开发者动作 | 是否顺畅 | Friction |
|------|-----------|---------|----------|
| 1 | 启动引擎 | ⚠️ | DX01 |
| 2 | 导入纹理资产 | ⚠️ | DX03 |
| 3 | 创建 Entity | ✅ | DX04 (LOW) |
| 4 | 分配纹理到实体 | ✅（经 Asset Browser） | DX05 (MEDIUM) |
| 5 | 编写 Lua 脚本 | ❌ | **DX06** |
| 6 | 附加脚本到实体 | ⚠️ | DX07 |
| 7 | Play / 测试 | ⚠️ | DX08 (LOW) |
| 8 | Save Scene | ✅ | — |
| 9 | 关闭并重新打开 | ✅ | — |
| 10 | 继续 Play | ✅ | — |

---

## Friction Ledger

### DX01 — 无"新建项目"/"空场景"选项 🔴 HIGH

**现象**: ScriptSandbox 启动时总是加载 sandbox_player.lua。首次用户看到别人的脚本在运行。
**理想行为**: 启动后显示空场景 + "New Project" 按钮。
**最小修复**: OnStartup 增加 `if (!std::filesystem::exists(kScenePath)) → 不加载默认脚本`。
**进 Ledger**: 是。

---

### DX02 — 输入提供者需要手动绑定 🟡 MEDIUM

**现象**: `GameplayAPI::Reset()` 清空 input provider 后必须手动调 `SetInputProvider()`，否则所有输入静默失效。
**影响**: 仅影响 headless 测试和底层集成；编辑器宿主自动处理。
**进 Ledger**: 否（编辑器已处理；headless 场景是开发者文档问题）。

---

### DX03 — 无"导入新资产"按钮/文件对话框 🟡 MEDIUM

**现象**: Asset Browser 只显示已在 Registry 中的资产。新资产必须手动输入路径到 Inspector 的文本框。
**理想行为**: "Import Asset..." 按钮打开文件对话框。
**最小修复**: 在 Asset Browser 工具栏添加 Import 按钮（调用 OS 文件对话框或至少列出 assets/ 目录内容）。
**进 Ledger**: 是。

---

### DX04 — 所有实体生成在原点 🟢 LOW

**现象**: HandleSpawn 不接受初始位置参数。
**绕路**: 立即切到 Inspector 手动设置位置。
**进 Ledger**: 低优先级（Inspector 可用）。

---

### DX05 — 同名实体导致绑定歧义 🟡 MEDIUM

**现象**: m_Bindings 按 handle 键控，但 SaveScene 的绑定对齐逻辑按 GetName() 匹配。两个同名实体会共享同一个绑定。
**风险**: 数据完整性。
**最小修复**: SaveScene 改为按 GetObjects() 顺序索引而非按名匹配。
**进 Ledger**: 是。

---

### DX06 — 无内置脚本编辑器 🔴 HIGH

**现象**: 编写/修改 Lua 脚本必须切换到外部编辑器。无语法检查直到运行时。无错误高亮。
**绕路**: F5 热重载可以快速迭代，但初次编写体验很差。
**最小修复**: 至少在 Console 中显示 Lua 加载/运行时的错误行号。
**完整方案**: 内嵌简化 Lua 编辑器（ImGui InputTextMultiline + 语法高亮）——但属于后续 DX 迭代。
**进 Ledger**: 是。

---

### DX07 — 脚本必须先 Import 才能附加 🟡 MEDIUM

**现象**: 开发者写了 .lua 文件后，期望"Attach to Entity"就够。但实际上必须先通过 Registry.Import() 注册 GUID，然后才能被 find() 或序列化引用。
**根因**: ContentRegistry 的身份模型要求所有引用走 GUID，但这个概念对新用户不直观。
**最小修复**: Inspector 的 Assign 按钮自动调用 Registry.Import()（当前已实现 ✓）。Asset Browser 也自动 Import 新文件。
**进 Ledger**: 部分——机制已实现但缺乏引导提示。

---

### DX08 — 无 Play/Pause 模式分离 🟢 LOW

**现象**: 游戏逻辑从启动开始持续运行，无法暂停检查状态或单步执行。
**绕路**: F5 重载脚本可重置状态。
**进 Ledger**: 低优先级（对 MVP 影响有限）。

---

## DX Baseline 总结

```text
总步骤数:          10
顺畅步骤:           4 (Step 3, 4, 8, 9-10)
有摩擦步骤:         6
─────────────────────────────
Friction 总数:      8
  HIGH (🔴):        2  (DX01, DX06)
  MEDIUM (🟡):      4  (DX02, DX03, DX05, DX07)
  LOW (🟢):         2  (DX04, DX08)
```

## Ledger 晋升候选

| DX# | 最小修复 | 估计工作量 | 建议 |
|-----|---------|-----------|------|
| DX01 | 空场景检测→跳过脚本加载 | ~10 行 C++ | **下一轮做** |
| DX03 | Asset Browser 添加目录扫描 | ~50 行 C++ | **下一轮做** |
| DX06 | Console 显示 Lua 错误行号 | ~20 行 C++ | 观察 |
| DX05 | SaveScene 按索引而非名称对齐 | ~15 行 C++ | **下一轮做** |

---

## 核心结论

> **引擎的核心能力已经足够支撑游戏制作。当前的瓶颈不是功能缺失，而是首次使用的引导和发现性。**

三个 Dogfood 的价值递进：

| Dogfood | 证明了什么 |
|---------|-----------|
| DF01 | Script API 够用，暴露 M001/M005 |
| DF02 | Resource 链路可靠，GUID 跨进程稳定 |
| DF03 | 复杂度提升后引擎仍稳定（13 实体+AI+计时器+胜负） |
| **DF04** | **开发体验的瓶颈是发现性和引导性，不是架构缺失** |

这是一个非常好的信号：**引擎的功能层已经"够用"，下一步应该投资的是让已有功能的入口更明显。**
