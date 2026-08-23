# Dogfood-01 — Content-Only 小游戏 · Missing Capability Ledger

> **日期**: 2026-08-23
> **定义**: 用冻结的 Scripting v1 + Content Pipeline v1 制作极小游戏过程中发现的每一个能力缺口。
> **规则**: 只记录真实开发中遇到的缺口，不做假设性推测。

## 游戏规格

Player（WASD 移动）穿越障碍物到达 Goal → Lua 检测胜利 → Console 输出 Victory。

## D1-D6 验收结果

| Gate | 要求 | 结果 |
|------|------|------|
| D1 | 不修改 Engine C++ Gameplay | ✅ 零引擎代码变更 |
| D2 | 全部 Entity 从 Scene 数据产生 | ✅ 手写 JSON 场景 |
| D3 | 玩法全部通过冻结 Scripting v1 | ✅ dogfood_game.lua 仅用 Engine.input/transform/log/random |
| D4 | 资产全部经 ContentRegistry | ✅ 清单 GUID 解析 |
| D5 | 关闭进程重新打开仍可 Play | ✅ Golden Gate 跨进程 PASS |
| D6 | 有真实游戏目标 | ✅ 到达 Goal → Victory 计数 |

---

## Missing Capability Ledger

### M001 — Entity 按名称查询 🔴 HIGH

**触发**: 脚本需要引用场景中的 Goal 实体，但无法按名查找。
**绕路**: 硬编码句柄序号（`_PERSIST.goalH = 5`），依赖领养顺序不变——脆弱。
**最小方案**: `Engine.entity.find(name) -> handle|nil`
**进 v2?**: 是。

### M002 — 组件数据序列化（半径/颜色等）🟡 MEDIUM

**触发**: 碰撞推挤需要每个实体的碰撞半径，但场景格式只有 Name+Position+GUID。
**绕路**: 半径硬编码为脚本常量，与实体实际大小脱钩。
**最小方案**: 场景格式增加 `"radius": float` 或通用 `"props": {}` 字典。
**进 v2?**: 是（作为 Component 序列化的一部分）。

### M003 — Prefab / Entity Template 🟢 LOW

**触发**: 需要放置 3 个 Obstacle——手写 JSON 三行重复条目。
**绕路**: 直接复制粘贴 JSON 块并修改坐标。3 个可接受；30 个会痛苦。
**结论**: 当前规模不需要。当同类实体 >10 时重启评估。
**进 v2?**: 暂不。

### M004 — Input Action Mapping / 边沿检测 🟡 MEDIUM

**触发**: 需要"按下瞬间"而非"持续按住"的输入检测（如跳跃）。
**绕路**: 使用 `Engine.input.is_down()` 的持续状态 + 自行在 `_PERSIST` 中追踪上一帧状态。
**最小方案**: `Engine.input.pressed(key)` 返回单帧边沿。
**进 v2?**: 是（一个函数即可，无需完整 Action Mapping 系统）。

### M005 — UI 文本显示（非 Log）🟡 MEDIUM

**触发**: 胜利消息只能通过 `Engine.log.info()` 写入后台控制台，玩家在游戏画面中看不到任何反馈。
**绕路**: 无。Console 在 ImGui 面板里可见但不是游戏 HUD。
**最小方案**: `Engine.ui.text(x, y, msg)` 或至少 `Engine.log.hud(msg)`。
**进 v2?**: 是（对游戏体验影响最大的单项缺失）。

### M006 — 工作目录锚定 🟡 MEDIUM

**触发**: 场景/清单使用相对路径，但运行结果取决于从哪个目录启动 exe。
**绕路**: 文档记录"必须从仓库根目录运行"或手动复制文件到 exe 目录。
**最小方案**: 引擎启动时将工作目录锚定到项目根（或可执行文件所在目录自动探测 assets/）。
**进 v2?**: 是（一行代码级别的修复但影响所有内容加载）。

---

## 优先级排序（基于 Dogfood 真实体验）

| 优先级 | 缺口 | 理由 |
|--------|------|------|
| P0 | M005 UI 文本 | 游戏无反馈=不可玩 |
| P0 | M001 Entity 查询 | 消除句柄硬编码脆弱性 |
| P1 | M006 工作目录 | 一行修复消除全部路径问题 |
| P1 | M004 Input pressed() | 一个函数解锁跳跃/射击类交互 |
| P2 | M002 组件数据 | 需要时再加 |
| P3 | M003 Prefab | 当前规模不需要 |

---

## 关键发现：Scripting v1 API 已足够支撑核心游戏循环

尽管有上述缺口，**移动→碰撞→胜利检测→计数** 这一核心循环完全用冻结 API 实现，
且行为确定性已验证。这意味着 Scripting v1 的架构方向是正确的——缺的是便利层，不是基础能力。
