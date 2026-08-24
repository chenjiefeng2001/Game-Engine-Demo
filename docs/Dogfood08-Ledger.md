# Dogfood-08 Ledger — Long-session DX（连续开发周期）

> 日期：2026-08-24 · 场景：Golden Scenario 全序列（Clean Start → … → 二次会话 → Final Round-trip）
> 载体：`Dogfood08.LongSession_EditorWorkflow` + 2 个专项探针（headless 模拟编辑器操作）
> 核心问题：**"开发者能不能连续用它做游戏，而不会被工具本身拖出工作流？"**

---

## 总评

**未出现需要立即实现新能力的阻塞。** 长会话全链路（脚本热改 ×2、场景中途变更、
跨进程重启 ×1、最终幂等回写）在冻结契约下全程无数据丢失。三项 deferred 信号
有轻微增强，仍均未达阈值。

```text
94/94 PASS · Integrity Gate I1(20 files)+I2+I3(DF01-08) ALL GREEN
```

---

## 一、状态可靠性 ✅（全部探针通过）

| 探针 | 结论 |
|------|------|
| R1 Reload 不增删场景对象 | ✅ count==3 恒定 |
| R2 Reload 不破坏未触碰实体位置 | ✅ Wall 位姿 bit-exact |
| R3 `_PERSIST` branch-1 保留 | ✅ 旧表携带 + 新代码写入并存 |
| S4 branch-2 显式新表让位旧表 | ✅ 与文档语义一致，无深合并歧义残留 |
| X1–X2 重启后实体数/名字全部存活 | ✅ 4 实体 find 全命中 |
| **X3 运行期位移穿越重启边界** | ✅ px 精确到 0.001 |
| X4 脚本绑定穿越重启 | ✅ ResolvePath 返回同一路径 |

**结论**：Reload/Save/Restart 三者的状态边界与文档声明完全一致，无隐式残留。

## 二、数据丢失风险 ⚠️→✅（本次最有价值的两项确认）

| 编号 | 发现 | 判定 |
|------|------|------|
| DL-01 | **Save 写的是活场景还是陈旧快照？** —— `CaptureScene(scene, bindings)` 存在且被测试强制：S2 探针验证运行位移（x≈4.0）而非 authored（0）被持久化，X3 验证其穿越重启 | ✅ 契约成立；但该契约**此前从未被任何测试钉住**——若有人改动 Capture 路径引入回归，DF08 是唯一防线 |
| DL-02 | **mid-session 新增实体的内容绑定依赖调用方维护 binding 表**——CaptureScene 对 `i >= bindings.size()` 静默降级为 Null GUID | ⚠️ 引擎层行为正确且优雅；**风险在编辑器层**：ScriptSandbox 若不同步维护 binding 表，用户"运行中加的对象"保存后 sprite/script 会静默丢失。已记入 Editor Workflow 待办观察，不构成引擎能力缺口 |
| DL-03 | `_PERSIST` 属进程内状态，重启重置 | ✅ 符合文档契约（Runtime state ≠ 持久资产），非数据丢失 |

## 三、工作流摩擦 👁（记录，不立项）

| 观测 | 说明 | 强度 |
|------|------|------|
| 分配步骤不可跳过 | 测试作者本人漏做 "Player←script 分配" 导致 X4 失败——真实新手也会在此处踩坑；当前由 Instantiate 后空绑定+警告承载可发现性 | 中 |
| Console 诊断质量 | `GetLastError().message` 含原始错误标记，语法/运行期错误均有诊断（ConsoleDiagnostics 探针） | ✅ 足够 |
| 手工路径/GUID 输入 | headless 序列中 Import/Assign 均走 UI 等价 API，无需接触 GUID 字符串 | ✅ 无新增摩擦 |
| Undo/Redo | 本次场景含两次"后悔药"时机（误改脚本/误移实体），靠重写文件解决；尚无一击即溃的痛点 | 低，维持不启动 |

## 四、证据质量 ✅

DF08 自身遵守 Integrity Gate：源文件注册（20/20）、discovery 计数入基线
（content 33→36）、I3 映射新增 DF08 行、门禁全绿后才出具本 Ledger。
测试编写过程中暴露的 2 个失败（漏写 diag.lua / 漏做分配步骤）均为
**测试侧流程缺失而非引擎缺陷**，恰好印证了分配步骤的摩擦观测。

---

## Deferred Ledger 复核

| 条目 | 变化 |
|------|------|
| M002 Component Serialization | 信号持平（本局无调参常量场景），维持 ⏸ |
| M003 Prefab | 未触发，维持 ⏸ |
| Undo/Redo | 新增弱信号（两次后悔药时机），维持不启动 👁 |
| Editor binding 表同步（DL-02） | **新增观察项**：属 Editor Workflow v1.1 范畴，非引擎能力 |
| 其余冻结契约 | 🔒 不变 |

## 下一步建议

DF08 证明长会话周期可靠。下一个自然维度是**多人协作/多文件规模**
（多场景互引、资产重命名传播、并发保存冲突），或按既定路线进入
M5 方向论证——但依旧：**只有真实阻塞才升级能力。**
